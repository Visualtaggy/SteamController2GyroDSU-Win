#include "axis_wizard.h"
#include "dsu_client.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProcess>
#include <QThread>
#include <cmath>
#include <algorithm>

namespace sc2 {

// ── Helpers ───────────────────────────────────────────────────────────────────

static Sc2Config identityConfig(const Sc2Config& base) {
    Sc2Config c = base;
    c.gyro  = {0, false, 1, false, 2, false};
    c.accel = {0, false, 1, false, 2, false};
    // Keep bias zero during detection
    c.gyroBias[0] = c.gyroBias[1] = c.gyroBias[2] = 0.f;
    return c;
}

static float sampleMean(const QVector<AxisWizard::Sample>& s, int comp) {
    if (s.isEmpty()) return 0.f;
    float sum = 0.f;
    for (const auto& x : s)
        sum += (comp < 3 ? x.a[comp] : x.g[comp - 3]);
    return sum / static_cast<float>(s.size());
}

static float sampleRms(const QVector<AxisWizard::Sample>& s, int comp) {
    if (s.isEmpty()) return 0.f;
    float sum = 0.f;
    for (const auto& x : s) {
        float v = (comp < 3 ? x.a[comp] : x.g[comp - 3]);
        sum += v * v;
    }
    return sqrtf(sum / static_cast<float>(s.size()));
}

// ── Construction ──────────────────────────────────────────────────────────────

AxisWizard::AxisWizard(const Sc2Config& current, QWidget* parent)
    : QDialog(parent), savedCfg_(current), result_(current)
{
    setWindowTitle("Axis Auto-Detect Wizard");
    setMinimumWidth(480);
    setMinimumHeight(320);
    setModal(true);

    auto* lay = new QVBoxLayout(this);
    lay->setSpacing(12);
    lay->setContentsMargins(20, 20, 20, 16);

    instrLbl_ = new QLabel;
    instrLbl_->setWordWrap(true);
    instrLbl_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    instrLbl_->setMinimumHeight(140);
    instrLbl_->setTextFormat(Qt::RichText);
    lay->addWidget(instrLbl_, 1);

    progBar_ = new QProgressBar;
    progBar_->setRange(0, 100);
    progBar_->setTextVisible(false);
    progBar_->setVisible(false);
    lay->addWidget(progBar_);

    statusLbl_ = new QLabel;
    statusLbl_->setWordWrap(true);
    statusLbl_->setAlignment(Qt::AlignCenter);
    statusLbl_->setTextFormat(Qt::RichText);
    lay->addWidget(statusLbl_);

    auto* btnRow = new QHBoxLayout;
    cancelBtn_ = new QPushButton("Cancel");
    connect(cancelBtn_, &QPushButton::clicked, this, &AxisWizard::onCancel);
    nextBtn_ = new QPushButton("Start");
    nextBtn_->setDefault(true);
    connect(nextBtn_, &QPushButton::clicked, this, &AxisWizard::onNext);
    btnRow->addWidget(cancelBtn_);
    btnRow->addStretch();
    btnRow->addWidget(nextBtn_);
    lay->addLayout(btnRow);

    collectTimer_ = new QTimer(this);
    collectTimer_->setInterval(80);
    connect(collectTimer_, &QTimer::timeout, this, &AxisWizard::onCollectTick);

    goToStage(INTRO);
}

AxisWizard::~AxisWizard() {
    collectTimer_->stop();
    if (client_) client_->stop();
}

// ── Stage machine ─────────────────────────────────────────────────────────────

void AxisWizard::goToStage(Stage s) {
    stage_ = s;
    collectTimer_->stop();
    progBar_->setVisible(false);
    nextBtn_->setEnabled(true);
    statusLbl_->clear();

    switch (s) {
    // ────────────────────────────────────────────────────────────────────────
    case INTRO:
        instrLbl_->setText(
            "<b style='font-size:14pt'>Axis Auto-Detect Wizard</b><br><br>"
            "This wizard guides you through four short gestures to automatically "
            "detect which raw sensor axes map to roll, pitch, and yaw.<br><br>"
            "The service will be <b>briefly restarted</b> with a neutral identity "
            "mapping during detection, then restored with the result.<br><br>"
            "<i>Make sure your Steam Controller 2 is connected before starting.</i>");
        nextBtn_->setText("Start");
        break;

    // ────────────────────────────────────────────────────────────────────────
    case WAIT_SERVICE:
        instrLbl_->setText(
            "<b>Applying neutral mapping and restarting service…</b><br><br>"
            "This takes about 2–3 seconds. Please wait.");
        nextBtn_->setEnabled(false);
        nextBtn_->setText("Next");
        progBar_->setVisible(true);
        progBar_->setValue(0);
        elapsedMs_ = 0;
        collectMs_ = 2800;
        activeSamples_ = nullptr;
        {
            Sc2Config id = identityConfig(savedCfg_);
            id.save();
            QProcess::execute("systemctl",
                {"--user", "restart", "SteamControllerGyroDSU.service"});
        }
        collectTimer_->start();
        break;

    // ────────────────────────────────────────────────────────────────────────
    case FLAT:
        instrLbl_->setText(
            "<b style='font-size:12pt'>① Place the controller flat, face up</b><br><br>"
            "Set it on a table or flat surface. Do <b>not</b> touch or tilt it.<br>"
            "Hold still for <b>5 seconds</b> while data is collected.");
        nextBtn_->setEnabled(false);
        progBar_->setVisible(true);
        progBar_->setValue(0);
        elapsedMs_ = 0;
        collectMs_ = 5000;
        flatSamples_.clear();
        activeSamples_ = &flatSamples_;
        collectTimer_->start();
        break;

    // ────────────────────────────────────────────────────────────────────────
    case TILT_RIGHT:
        instrLbl_->setText(
            "<b style='font-size:12pt'>② Tilt the RIGHT SIDE down</b><br><br>"
            "Pick up the controller and tilt the right side down "
            "by about <b>30–45°</b>. Hold that angle still for <b>5 seconds</b>.");
        nextBtn_->setEnabled(false);
        progBar_->setVisible(true);
        progBar_->setValue(0);
        elapsedMs_ = 0;
        collectMs_ = 5000;
        tiltRightSamples_.clear();
        activeSamples_ = &tiltRightSamples_;
        collectTimer_->start();
        break;

    // ────────────────────────────────────────────────────────────────────────
    case TILT_TOP:
        instrLbl_->setText(
            "<b style='font-size:12pt'>③ Tilt the TOP EDGE away from you</b><br><br>"
            "Lay it flat again, then tilt the top edge (shoulder buttons) "
            "<b>away</b> from you by about 30–45°. Hold still for <b>5 seconds</b>.");
        nextBtn_->setEnabled(false);
        progBar_->setVisible(true);
        progBar_->setValue(0);
        elapsedMs_ = 0;
        collectMs_ = 5000;
        tiltTopSamples_.clear();
        activeSamples_ = &tiltTopSamples_;
        collectTimer_->start();
        break;

    // ────────────────────────────────────────────────────────────────────────
    case SPIN:
        instrLbl_->setText(
            "<b style='font-size:12pt'>④ Spin it clockwise on the table</b><br><br>"
            "Lay it flat and spin the controller <b>clockwise</b> "
            "(viewed from above) — keep spinning for <b>5 seconds</b>.<br>"
            "This detects the yaw direction.");
        nextBtn_->setEnabled(false);
        progBar_->setVisible(true);
        progBar_->setValue(0);
        elapsedMs_ = 0;
        collectMs_ = 5000;
        spinSamples_.clear();
        activeSamples_ = &spinSamples_;
        collectTimer_->start();
        break;

    // ────────────────────────────────────────────────────────────────────────
    case RESULT:
        if (buildMapping()) {
            instrLbl_->setText(
                "<b style='font-size:12pt;color:green'>✓ Mapping detected!</b><br><br>"
                + resultSummary_ +
                "<br><br>Click <b>Apply</b> to save, or <b>Cancel</b> to restore "
                "the original mapping.");
            statusLbl_->setText(
                "<i>Gyro roll/pitch inversions are estimated — "
                "verify with the 3D wireframe in the Test / Verify tab.</i>");
            nextBtn_->setText("Apply");
        } else {
            instrLbl_->setText(
                "<b style='color:red'>✗ Detection failed</b><br><br>"
                + detectError_ +
                "<br><br>Click <b>Retry</b> to start over.");
            nextBtn_->setText("Retry");
        }
        break;
    }
}

// ── Timer / data collection ───────────────────────────────────────────────────

void AxisWizard::onCollectTick() {
    elapsedMs_ += 80;
    progBar_->setValue(std::min(elapsedMs_ * 100 / collectMs_, 100));

    if (elapsedMs_ < collectMs_) return;

    collectTimer_->stop();

    switch (stage_) {
    case WAIT_SERVICE:
        // Service should be up — create client and move to first gesture
        if (!client_) {
            client_ = new DsuClient(this);
            connect(client_, &DsuClient::sampleReceived,
                    this, &AxisWizard::onSample);
            client_->start(savedCfg_.port);
        }
        goToStage(FLAT);
        break;

    case FLAT:       checkStageComplete(TILT_RIGHT, "flat");       break;
    case TILT_RIGHT: checkStageComplete(TILT_TOP,   "tilt-right"); break;
    case TILT_TOP:   checkStageComplete(SPIN,        "tilt-top");  break;
    case SPIN:       goToStage(RESULT);                            break;
    default: break;
    }
}

void AxisWizard::checkStageComplete(Stage next, const char* label) {
    int n = activeSamples_ ? activeSamples_->size() : 0;
    if (n < 20) {
        statusLbl_->setText(QString(
            "<b style='color:orange'>⚠ Only %1 samples received during '%2'. "
            "Is the controller connected?</b>").arg(n).arg(label));
    }
    goToStage(next);
}

void AxisWizard::onSample(int /*slot*/,
    float ax, float ay, float az,
    float gx, float gy, float gz, quint64 /*ts*/)
{
    if (activeSamples_)
        activeSamples_->append(Sample{{ax, ay, az}, {gx, gy, gz}});
}

// ── Detection algorithm ───────────────────────────────────────────────────────

bool AxisWizard::buildMapping() {
    // ── 1: gravity axis (flat, face-up) ─────────────────────────────────────
    float flatA[3];
    for (int i = 0; i < 3; ++i) flatA[i] = sampleMean(flatSamples_, i);

    int gravAxis = 0;
    for (int i = 1; i < 3; ++i)
        if (fabsf(flatA[i]) > fabsf(flatA[gravAxis])) gravAxis = i;

    if (fabsf(flatA[gravAxis]) < 0.6f) {
        detectError_ =
            "Could not find the gravity axis — the controller may not have been "
            "held still, or the service is not sending data.";
        return false;
    }
    // Invert if gravity registered as negative (DSU convention: flat face-up = +1g on accel_z)
    bool gravInv = (flatA[gravAxis] < 0.f);

    // ── 2: roll axis (tilt right side down) ──────────────────────────────────
    float rightA[3], rightG[3];
    for (int i = 0; i < 3; ++i) {
        rightA[i] = sampleMean(tiltRightSamples_, i);
        rightG[i] = sampleMean(tiltRightSamples_, i + 3);
    }

    // Find the non-gravity axis with the largest delta from flat
    float delta[3];
    for (int i = 0; i < 3; ++i) delta[i] = rightA[i] - flatA[i];
    delta[gravAxis] = 0.f;

    int rollAxis = (gravAxis == 0) ? 1 : 0;
    for (int i = 0; i < 3; ++i)
        if (i != gravAxis && fabsf(delta[i]) > fabsf(delta[rollAxis]))
            rollAxis = i;

    if (fabsf(delta[rollAxis]) < 0.2f) {
        detectError_ =
            "Could not detect the roll axis — tilt the right side down "
            "at least 20° and hold it still.";
        return false;
    }
    // AccelRightToLeft: positive when right side is down.
    bool rollAccelInv = (delta[rollAxis] < 0.f);
    // Gyro: best-effort from transient at start of motion
    bool rollGyroInv  = (rightG[rollAxis] < 0.f);

    // ── 3: pitch axis (the remaining axis) ──────────────────────────────────
    int pitchAxis = 3 - gravAxis - rollAxis;  // 0+1+2 = 3, so remainder is unambiguous

    float topA[3], topG[3];
    for (int i = 0; i < 3; ++i) {
        topA[i] = sampleMean(tiltTopSamples_, i);
        topG[i] = sampleMean(tiltTopSamples_, i + 3);
    }

    float pitchDelta = topA[pitchAxis] - flatA[pitchAxis];
    bool pitchAccelInv = (pitchDelta < 0.f);
    bool pitchGyroInv  = (topG[pitchAxis] < 0.f);

    if (fabsf(pitchDelta) < 0.15f) {
        statusLbl_->setText(
            "<i>⚠ Weak pitch signal — inversion may be wrong. "
            "Verify with the wireframe in the Test tab.</i>");
    }

    // ── 4: yaw axis and sign (spinning clockwise from above) ─────────────────
    // Yaw = rotation around the gravity axis. Confirm from spin stage.
    float spinRms[3], spinMean[3];
    for (int i = 0; i < 3; ++i) {
        spinRms[i]  = sampleRms( spinSamples_, i + 3);
        spinMean[i] = sampleMean(spinSamples_, i + 3);
    }

    // The spin axis with highest RMS should match gravAxis; use whichever is stronger.
    int yawAxis = gravAxis;
    if (spinRms[(yawAxis + 1) % 3] > spinRms[yawAxis] * 1.5f)
        yawAxis = (gravAxis + 1) % 3;
    if (spinRms[(yawAxis + 2) % 3] > spinRms[yawAxis] * 1.5f)
        yawAxis = (gravAxis + 2) % 3;

    // CW spin from above = negative yaw in our rotate() convention (positive yaw = CCW).
    // So if spinMean is positive the user spun CW → we need to invert.
    bool yawInv = (spinMean[yawAxis] > 0.f);

    if (spinRms[yawAxis] < 5.f) {
        // Keep existing status message if pitch already set one, otherwise add one
        if (statusLbl_->text().isEmpty())
            statusLbl_->setText(
                "<i>⚠ Weak yaw signal — yaw inversion may be wrong. "
                "Verify with the wireframe in the Test tab.</i>");
    }

    // ── Build AxisMap ─────────────────────────────────────────────────────────
    result_ = savedCfg_;
    result_.accel = {rollAxis,  rollAccelInv,
                     pitchAxis, pitchAccelInv,
                     gravAxis,  gravInv};
    // DSU/controller_view convention: gyro[0]=pitch, gyro[1]=yaw, gyro[2]=roll.
    // (Matches DEFAULT_GYRO: src_x=pitch_axis, src_y=yaw_axis, src_z=roll_axis.)
    result_.gyro  = {pitchAxis, pitchGyroInv,
                     yawAxis,   yawInv,
                     rollAxis,  rollGyroInv};

    const char* ax[3] = {"0", "1", "2"};
    resultSummary_ = QString(
        "<table cellspacing='4'>"
        "<tr><th align='left'>Output</th>"
        "    <th align='left'>Raw axis</th>"
        "    <th align='left'>Invert</th></tr>"
        "<tr><td>Accel X (roll)</td>       <td>%1</td><td>%2</td></tr>"
        "<tr><td>Accel Y (pitch)</td>      <td>%3</td><td>%4</td></tr>"
        "<tr><td>Accel Z (gravity)</td>    <td>%5</td><td>%6</td></tr>"
        "<tr><td>Gyro X (pitch rate)</td>  <td>%7</td><td>%8</td></tr>"
        "<tr><td>Gyro Y (yaw rate)</td>    <td>%9</td><td>%10</td></tr>"
        "<tr><td>Gyro Z (roll rate)</td>   <td>%11</td><td>%12</td></tr>"
        "</table>")
        .arg(ax[rollAxis]).arg(rollAccelInv   ? "yes" : "no")
        .arg(ax[pitchAxis]).arg(pitchAccelInv ? "yes" : "no")
        .arg(ax[gravAxis]).arg(gravInv        ? "yes" : "no")
        .arg(ax[pitchAxis]).arg(pitchGyroInv  ? "yes" : "no")
        .arg(ax[yawAxis]).arg(yawInv          ? "yes" : "no")
        .arg(ax[rollAxis]).arg(rollGyroInv    ? "yes" : "no");

    return true;
}

// ── Button handlers ───────────────────────────────────────────────────────────

void AxisWizard::onNext() {
    if (stage_ == INTRO) {
        goToStage(WAIT_SERVICE);
    } else if (stage_ == RESULT) {
        if (nextBtn_->text() == "Retry") {
            if (client_) { client_->stop(); delete client_; client_ = nullptr; }
            goToStage(WAIT_SERVICE);
        } else {
            accept();   // caller reads detectedConfig()
        }
    }
}

void AxisWizard::onCancel() {
    collectTimer_->stop();
    if (client_) { client_->stop(); }
    // Restore original mapping and restart
    savedCfg_.save();
    QProcess::execute("systemctl",
        {"--user", "restart", "SteamControllerGyroDSU.service"});
    reject();
}

} // namespace sc2
