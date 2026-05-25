#include "controller_view.h"
#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <cmath>
#include <algorithm>

namespace sc2 {

// ── Complementary filter tuning ───────────────────────────────────────────────
static constexpr float ALPHA    = 0.96f;   // gyro weight per update
static constexpr float MAX_DT   = 0.05f;   // cap dt to avoid large jumps

// ── SC2 wireframe geometry (local model space) ────────────────────────────────
// Controller rests in the XZ plane (X=right, Z=up on screen when upright).
// Y axis points toward the user (out of the screen / controller face direction).
// Scale: 1.0 ≈ half the controller width.
//
// Body outline vertices (front face, Z=+0.08):
static const ControllerView::V3 BODY[] = {
    // Shoulder / top edge
    {-0.70f,  0.08f,  0.47f}, // 0  top-left shoulder
    {-0.32f,  0.08f,  0.56f}, // 1  top-left center
    { 0.32f,  0.08f,  0.56f}, // 2  top-right center
    { 0.70f,  0.08f,  0.47f}, // 3  top-right shoulder
    // Right side
    { 0.80f,  0.08f,  0.22f}, // 4  right upper
    { 0.80f,  0.08f, -0.12f}, // 5  right
    { 0.58f,  0.08f, -0.32f}, // 6  right waist
    // Right grip
    { 0.55f,  0.08f, -0.82f}, // 7  right grip outer
    { 0.35f,  0.08f, -0.90f}, // 8  right grip bottom
    { 0.22f,  0.08f, -0.84f}, // 9  right grip inner
    // Left grip
    {-0.22f,  0.08f, -0.84f}, // 10 left grip inner
    {-0.35f,  0.08f, -0.90f}, // 11 left grip bottom
    {-0.55f,  0.08f, -0.82f}, // 12 left grip outer
    // Left side
    {-0.58f,  0.08f, -0.32f}, // 13 left waist
    {-0.80f,  0.08f, -0.12f}, // 14 left
    {-0.80f,  0.08f,  0.22f}, // 15 left upper
};
static const int BODY_N = 16;

// Back face at Y=-0.08, same XZ:
static ControllerView::V3 backV(ControllerView::V3 v) {
    return { v.x, -0.08f, v.z };
}

// Analog stick positions (center, radius)
static const ControllerView::V3 LS_CTR = {-0.42f,  0.08f,  0.14f};
static const ControllerView::V3 RS_CTR = { 0.20f,  0.08f, -0.20f};
static constexpr float STICK_R = 0.13f;

// D-pad cross center
static const ControllerView::V3 DPAD_CTR = {-0.28f, 0.08f, -0.30f};
static constexpr float DPAD_ARM = 0.09f;
static constexpr float DPAD_W   = 0.04f;

// Face button centers (A, B, X, Y)
static const ControllerView::V3 BTN_CTR = { 0.42f, 0.08f, 0.14f};
static constexpr float BTN_R = 0.055f;
static const float BTN_OFF = 0.095f;

// ── ControllerView ────────────────────────────────────────────────────────────

ControllerView::ControllerView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 160);
    QPalette p = palette();
    p.setColor(QPalette::Window, QColor(0x1a, 0x1a, 0x2e));
    setPalette(p);
    setAutoFillBackground(true);
}

void ControllerView::resetFilter() {
    roll_ = pitch_ = yaw_ = 0.f;
    filterInit_ = false;
}

// ── Math helpers ──────────────────────────────────────────────────────────────

// Apply pitch→roll→yaw Euler rotation to a point in model space.
//
// Model axes:  X = right,  Y = toward camera (face direction),  Z = up on screen.
//
//   pitch  — rotation around X (side axis):    positive = top tilts away from camera
//   roll   — rotation around Y (face axis):    positive = right side tilts down
//   yaw    — rotation around Z (vertical axis): positive = CCW from above (right swings toward camera)
ControllerView::V3 ControllerView::rotate(V3 v) const {
    // 1. Pitch around X axis (side — tilts top toward/away from camera)
    float cp = cosf(pitch_), sp = sinf(pitch_);
    float y1 =  cp * v.y - sp * v.z;
    float z1 =  sp * v.y + cp * v.z;
    float x1 =  v.x;

    // 2. Roll around Y axis (face/depth — tilts left/right)
    float cr = cosf(roll_),  sr = sinf(roll_);
    float x2 =  cr * x1 + sr * z1;
    float z2 = -sr * x1 + cr * z1;
    float y2 =  y1;

    // 3. Yaw around Z axis (vertical — spins like a top)
    float cy = cosf(yaw_),   sy = sinf(yaw_);
    float x3 =  cy * x2 - sy * y2;
    float y3 =  sy * x2 + cy * y2;
    float z3 =  z2;

    return { x3, y3, z3 };
}

// Perspective projection. Camera sits at (0,0,3.5) looking toward -Z in view space.
// Model-space Y → screen up, model-space Z → depth.
QPointF ControllerView::project(V3 v, float cx, float cy, float scale) const {
    const float camDist = 3.5f;
    float w = camDist - v.y;               // distance from camera
    float sx = cx + scale * v.x / w;
    float sy = cy - scale * v.z / w;       // Z up → screen up
    return {sx, sy};
}

void ControllerView::drawLine(QPainter& p, V3 a, V3 b,
                               float cx, float cy, float scale) const {
    V3 ra = rotate(a), rb = rotate(b);
    p.drawLine(project(ra, cx, cy, scale), project(rb, cx, cy, scale));
}

// Draw a circle in the XZ plane (face plane of the model) as a polygon.
void ControllerView::drawCircle(QPainter& p, V3 center, float r, V3 /*normalAxis*/,
                                float cx, float cy, float scale, int segs) const {
    QPolygonF poly;
    for (int i = 0; i < segs; i++) {
        float angle = float(i) / float(segs) * 2.f * float(M_PI);
        V3 pt = { center.x + r * cosf(angle),
                   center.y,
                   center.z + r * sinf(angle) };
        poly << project(rotate(pt), cx, cy, scale);
    }
    poly << poly[0];
    p.drawPolyline(poly);
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void ControllerView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const float cx    = width()  * 0.5f;
    const float cy    = height() * 0.5f;
    const float scale = std::min(width(), height()) * 0.42f;

    // ── Determine front/back face visibility via face normal ──────────────────
    V3 faceNorm = rotate({0.f, 1.f, 0.f});   // model +Y = face direction
    bool facingUser = (faceNorm.y > 0.f);     // Y > 0 means tilting toward camera

    QColor colFront(0x00, 0xff, 0x88);   // bright green for front face
    QColor colBack(0x00, 0x88, 0x44);    // darker green for back
    QColor colEdge(0x00, 0xcc, 0x66);    // medium for connecting edges

    // ── Draw back face ────────────────────────────────────────────────────────
    {
        QPen pen(facingUser ? colBack : colFront, 1.2f);
        p.setPen(pen);
        for (int i = 0; i < BODY_N; i++) {
            V3 a = backV(BODY[i]);
            V3 b = backV(BODY[(i + 1) % BODY_N]);
            drawLine(p, a, b, cx, cy, scale);
        }
    }

    // ── Draw connecting edges (front ↔ back) ──────────────────────────────────
    {
        QPen pen(colEdge, 1.0f);
        p.setPen(pen);
        // Only draw edges at selected vertices (avoid clutter)
        const int edges[] = {0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
        for (int i : edges)
            drawLine(p, BODY[i], backV(BODY[i]), cx, cy, scale);
    }

    // ── Draw front face ───────────────────────────────────────────────────────
    {
        QPen pen(facingUser ? colFront : colBack, 1.8f);
        p.setPen(pen);
        for (int i = 0; i < BODY_N; i++) {
            drawLine(p, BODY[i], BODY[(i + 1) % BODY_N], cx, cy, scale);
        }
    }

    // ── Front-face features (sticks, d-pad, buttons) ─────────────────────────
    {
        QColor featColor(facingUser ? colFront : colBack);
        featColor.setAlphaF(facingUser ? 0.9f : 0.4f);
        QPen pen(featColor, 1.4f);
        p.setPen(pen);

        // Left and right sticks
        V3 lsNorm = {0.f, 1.f, 0.f};
        drawCircle(p, LS_CTR, STICK_R, lsNorm, cx, cy, scale, 16);
        drawCircle(p, RS_CTR, STICK_R, lsNorm, cx, cy, scale, 16);

        // D-pad cross
        auto dc = DPAD_CTR;
        drawLine(p, {dc.x, dc.y, dc.z - DPAD_ARM}, {dc.x, dc.y, dc.z + DPAD_ARM}, cx, cy, scale);
        drawLine(p, {dc.x - DPAD_ARM, dc.y, dc.z}, {dc.x + DPAD_ARM, dc.y, dc.z}, cx, cy, scale);

        // ABXY button cluster (4 circles)
        static const float bx[] = { BTN_OFF, -BTN_OFF, 0,        0       };
        static const float bz[] = { 0,        0,        BTN_OFF, -BTN_OFF};
        for (int i = 0; i < 4; i++) {
            V3 bc = { BTN_CTR.x + bx[i], BTN_CTR.y, BTN_CTR.z + bz[i] };
            drawCircle(p, bc, BTN_R, {0,1,0}, cx, cy, scale, 8);
        }

        // Steam button (small circle, center top)
        drawCircle(p, {0.0f, 0.08f, 0.22f}, 0.06f, {0,1,0}, cx, cy, scale, 8);
        // QAM button
        drawCircle(p, {0.12f, 0.08f, 0.22f}, 0.04f, {0,1,0}, cx, cy, scale, 8);
        // View button
        drawCircle(p, {-0.12f, 0.08f, 0.22f}, 0.04f, {0,1,0}, cx, cy, scale, 8);
    }

    // ── Orientation indicator: roll/pitch arc ─────────────────────────────────
    {
        // Small horizon indicator in the corner
        const int hx = width() - 54, hy = 42, hr = 34;
        p.save();
        p.translate(hx, hy);

        QPen arcPen(QColor(0x44, 0x88, 0xff), 1.5f);
        p.setPen(arcPen);

        // Horizon line (rotates with roll — positive roll = right side down = CW tilt)
        float rollDeg = roll_ * 180.f / float(M_PI);
        p.save();
        p.rotate(rollDeg);
        p.drawLine(-hr, 0, hr, 0);
        // Pitch tick
        float pitchPx = pitch_ * 180.f / float(M_PI) * hr / 90.f;
        p.drawLine(-6, int(pitchPx), 6, int(pitchPx));
        p.restore();

        // Reference cross
        QPen refPen(QColor(0xff, 0x88, 0x00, 150), 1.0f);
        p.setPen(refPen);
        p.drawLine(-6, 0, 6, 0);
        p.drawLine(0, -6, 0, 6);

        // Circle border
        QPen borPen(QColor(0x44, 0x44, 0x66), 1.0f);
        p.setPen(borPen);
        p.drawEllipse(-hr, -hr, hr*2, hr*2);

        p.restore();
    }
}

// ── Complementary filter ──────────────────────────────────────────────────────

void ControllerView::pushSample(float accel[3], float gyro[3], quint64 elapsed_us) {
    std::copy(accel, accel+3, lastAccel_);
    std::copy(gyro,  gyro+3,  lastGyro_);

    float dt = float(elapsed_us) / 1e6f;
    if (dt <= 0.f || dt > MAX_DT) dt = 0.008f;

    // DSU-space:
    //   accel_x positive → AccelRightToLeft  (right side down)
    //   accel_y positive → AccelFrontToBack  (face down)
    //   accel_z positive → AccelTopToBottom  (face-up = +1g)
    //
    // We want the gravity vector to drive the visualization.
    // The accelerometer measures the reaction to gravity (proper acceleration).
    // When face-up flat: accel_z ≈ +1g (top-to-bottom force is gravity,
    // but proper acceleration = upward = +Z in DSU convention).
    //
    // Compute accel-derived roll (around the long Y/face axis in our model)
    // and pitch (around the side X axis).
    //
    // In model space: X=right, Z=up-on-screen, Y=toward-camera.
    // Map DSU → model:  dsu_x → model_x (side),
    //                   dsu_y → -model_z (face/depth),
    //                   dsu_z → model_z-ish (but dsu_z = gravity up when flat).
    //
    // roll  from accel: tilt left-right = rotation around z-axis in model space
    //   roll_acc = atan2(ax, az)   [right-hand: positive ax = tilted right → positive roll]
    // pitch from accel: tilt nose up/down = rotation around x-axis in model space
    //   pitch_acc = atan2(-ay, az) [positive ay = face tilted down → positive pitch]

    float norm = sqrtf(accel[0]*accel[0] + accel[1]*accel[1] + accel[2]*accel[2]);
    if (norm < 0.3f || norm > 3.0f) {
        // Too much motion or sensor dropout — integrate gyro only
        roll_  += gyro[0] * dt * float(M_PI)/180.f;
        pitch_ += gyro[1] * dt * float(M_PI)/180.f;
        yaw_   += gyro[2] * dt * float(M_PI)/180.f;
        update();
        return;
    }

    float ax = accel[0] / norm;
    float ay = accel[1] / norm;
    float az = accel[2] / norm;

    float roll_acc  = atan2f( ax,  az);
    float pitch_acc = atan2f(-ay,  az);

    // DSU gyro axis → physical rotation mapping (from DEFAULT_GYRO src assignments):
    //   gyro[0] = raw[0] (side axis)       → rotation around X → pitch rate
    //   gyro[1] = -raw[2] (top-bottom axis) → rotation around Z → -yaw rate (inverted in DEFAULT_GYRO)
    //   gyro[2] = raw[1] (face-depth axis)  → rotation around Y → roll rate
    float pitch_rate = gyro[0] * float(M_PI)/180.f;
    float roll_rate  = gyro[2] * float(M_PI)/180.f;
    float yaw_rate   = -gyro[1] * float(M_PI)/180.f;  // negated: DEFAULT_GYRO inv_y=true

    if (!filterInit_) {
        roll_  = roll_acc;
        pitch_ = pitch_acc;
        yaw_   = 0.f;
        filterInit_ = true;
    } else {
        roll_  = ALPHA * (roll_  + roll_rate  * dt) + (1.f - ALPHA) * roll_acc;
        pitch_ = ALPHA * (pitch_ + pitch_rate * dt) + (1.f - ALPHA) * pitch_acc;
        yaw_  += yaw_rate * dt;
    }

    update();
}

} // namespace sc2
