#include "controller_view.h"
#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <cmath>
#include <algorithm>

namespace sc2 {

// ── Complementary filter tuning ───────────────────────────────────────────────
static constexpr float ALPHA  = 0.96f;   // gyro weight per update
static constexpr float MAX_DT = 0.05f;   // cap dt to avoid large jumps

// ── SC2026 wireframe geometry (local model space) ─────────────────────────────
// Controller rests in the XZ plane (X=right, Z=up on screen when upright).
// Y axis points toward the user (out of the screen / controller face direction).
// Scale: 1.0 ≈ half the controller width.
//
// Body outline vertices (front face, Y=+0.08) — 20-point clockwise outline.
// The SC2026 silhouette: wide arched top (large trackpad area), tapered waist,
// then two grip prongs at the bottom.
static const ControllerView::V3 BODY[] = {
    // ── Top arch ──
    {-0.66f,  0.08f,  0.60f}, //  0  top-left shoulder
    {-0.28f,  0.08f,  0.70f}, //  1  top center-left
    { 0.28f,  0.08f,  0.70f}, //  2  top center-right
    { 0.66f,  0.08f,  0.60f}, //  3  top-right shoulder
    // ── Right side arc → waist ──
    { 0.88f,  0.08f,  0.36f}, //  4  right upper arc
    { 0.92f,  0.08f,  0.06f}, //  5  right side (widest)
    { 0.82f,  0.08f, -0.14f}, //  6  right waist upper
    { 0.64f,  0.08f, -0.28f}, //  7  right waist lower
    // ── Right grip prong ──
    { 0.62f,  0.08f, -0.68f}, //  8  right grip outer
    { 0.46f,  0.08f, -0.88f}, //  9  right grip bottom-outer
    { 0.26f,  0.08f, -0.94f}, // 10  right grip bottom
    { 0.08f,  0.08f, -0.88f}, // 11  right grip inner
    // ── Left grip prong (mirrored) ──
    {-0.08f,  0.08f, -0.88f}, // 12  left grip inner
    {-0.26f,  0.08f, -0.94f}, // 13  left grip bottom
    {-0.46f,  0.08f, -0.88f}, // 14  left grip bottom-outer
    {-0.62f,  0.08f, -0.68f}, // 15  left grip outer
    // ── Left side arc ← waist ──
    {-0.64f,  0.08f, -0.28f}, // 16  left waist lower
    {-0.82f,  0.08f, -0.14f}, // 17  left waist upper
    {-0.92f,  0.08f,  0.06f}, // 18  left side (widest)
    {-0.88f,  0.08f,  0.36f}, // 19  left upper arc
};
static const int BODY_N = 20;

// Back face at Y=-0.08, same XZ:
static ControllerView::V3 backV(ControllerView::V3 v) {
    return { v.x, -0.08f, v.z };
}

// ── SC2026 surface features ───────────────────────────────────────────────────

// Large circular trackpads (the signature SC2026 feature)
static const ControllerView::V3 LP_CTR = {-0.36f,  0.08f,  0.24f}; // left pad
static const ControllerView::V3 RP_CTR = { 0.36f,  0.08f,  0.24f}; // right pad
static constexpr float PAD_R = 0.22f;
static constexpr int   PAD_SEGS = 24;

// Thumbsticks (below and inner relative to trackpads)
static const ControllerView::V3 LS_CTR = {-0.18f,  0.08f, -0.06f}; // left stick
static const ControllerView::V3 RS_CTR = { 0.18f,  0.08f, -0.06f}; // right stick
static constexpr float STICK_R = 0.08f;

// D-pad cross (left outer area)
static const ControllerView::V3 DPAD_CTR = {-0.68f, 0.08f,  0.24f};
static constexpr float DPAD_ARM = 0.07f;
static constexpr float DPAD_W   = 0.03f;

// ABXY face button cluster (right outer area)
static const ControllerView::V3 BTN_CTR = { 0.68f, 0.08f,  0.24f};
static constexpr float BTN_R   = 0.045f;
static constexpr float BTN_OFF = 0.078f;

// Center buttons (Steam, quick-access, view)
static const ControllerView::V3 STEAM_CTR = { 0.00f, 0.08f,  0.08f};
static const ControllerView::V3 QAM_CTR   = { 0.13f, 0.08f,  0.08f};
static const ControllerView::V3 VIEW_CTR  = {-0.13f, 0.08f,  0.08f};

// ── ControllerView ────────────────────────────────────────────────────────────

ControllerView::ControllerView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 160);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x1a, 0x1a, 0x2e));
    setPalette(pal);
    setAutoFillBackground(true);
}

void ControllerView::resetFilter() {
    roll_ = pitch_ = yaw_ = 0.f;
    filterInit_ = false;
}

// ── Math helpers ──────────────────────────────────────────────────────────────

// Apply pitch → roll → yaw Euler rotation to a point in model space.
//
// Model axes: X = right,  Y = toward camera (face direction),  Z = up on screen.
//
//   pitch  — rotation around X (side axis):     positive = top tilts away from camera
//   roll   — rotation around Y (face axis):     positive = right side tilts down
//   yaw    — rotation around Z (vertical axis): positive = CCW from above
ControllerView::V3 ControllerView::rotate(V3 v) const {
    // 1. Pitch around X axis
    float cp = cosf(pitch_), sp = sinf(pitch_);
    float y1 =  cp * v.y - sp * v.z;
    float z1 =  sp * v.y + cp * v.z;
    float x1 =  v.x;

    // 2. Roll around Y axis
    float cr = cosf(roll_),  sr = sinf(roll_);
    float x2 =  cr * x1 + sr * z1;
    float z2 = -sr * x1 + cr * z1;
    float y2 =  y1;

    // 3. Yaw around Z axis
    float cy = cosf(yaw_),   sy = sinf(yaw_);
    float x3 =  cy * x2 - sy * y2;
    float y3 =  sy * x2 + cy * y2;
    float z3 =  z2;

    return { x3, y3, z3 };
}

// Perspective projection. Camera at (0, 3.5, 0) looking toward model origin.
// Model Z is up on screen; model X is right.
QPointF ControllerView::project(V3 v, float cx, float cy, float scale) const {
    const float camDist = 3.5f;
    float w  = camDist - v.y;           // distance from camera
    float sx = cx + scale * v.x / w;
    float sy = cy - scale * v.z / w;   // Z up → screen up
    return {sx, sy};
}

void ControllerView::drawLine(QPainter& p, V3 a, V3 b,
                               float cx, float cy, float scale) const {
    p.drawLine(project(rotate(a), cx, cy, scale),
               project(rotate(b), cx, cy, scale));
}

// Draw a circle in the face (XZ) plane of the model as a projected polyline.
void ControllerView::drawCircle(QPainter& p, V3 center, float r, V3 /*normal*/,
                                float cx, float cy, float scale, int segs) const {
    QPolygonF poly;
    poly.reserve(segs + 1);
    for (int i = 0; i < segs; ++i) {
        float a = float(i) / float(segs) * 2.f * float(M_PI);
        V3 pt = { center.x + r * cosf(a), center.y, center.z + r * sinf(a) };
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
    const float cy    = height() * 0.52f;   // slight vertical offset
    const float scale = std::min(width(), height()) * 0.40f;

    // ── Face visibility from face normal ──────────────────────────────────────
    V3 faceNorm = rotate({0.f, 1.f, 0.f});  // model +Y = face direction
    bool facingUser = (faceNorm.y > 0.f);

    QColor colFront(0x00, 0xff, 0x88);   // bright green — front face
    QColor colBack (0x00, 0x88, 0x44);   // darker green — back face
    QColor colEdge (0x00, 0xcc, 0x66);   // medium — connecting edges

    // ── Back face ─────────────────────────────────────────────────────────────
    {
        QPen pen(facingUser ? colBack : colFront, 1.2f);
        p.setPen(pen);
        for (int i = 0; i < BODY_N; ++i)
            drawLine(p, backV(BODY[i]), backV(BODY[(i + 1) % BODY_N]), cx, cy, scale);
    }

    // ── Connecting edges (front ↔ back) ───────────────────────────────────────
    {
        QPen pen(colEdge, 1.0f);
        p.setPen(pen);
        // Sparse selection — shoulders, waist notch, grip corners
        const int edges[] = {0, 3, 4, 5, 7, 8, 10, 11, 12, 13, 15, 16, 17, 18, 19};
        for (int i : edges)
            drawLine(p, BODY[i], backV(BODY[i]), cx, cy, scale);
    }

    // ── Front face ────────────────────────────────────────────────────────────
    {
        QPen pen(facingUser ? colFront : colBack, 1.8f);
        p.setPen(pen);
        for (int i = 0; i < BODY_N; ++i)
            drawLine(p, BODY[i], BODY[(i + 1) % BODY_N], cx, cy, scale);
    }

    // ── Front-face features ───────────────────────────────────────────────────
    {
        float alpha = facingUser ? 0.9f : 0.4f;
        QColor featCol(colFront);
        featCol.setAlphaF(alpha);
        QPen pen(featCol, 1.5f);
        p.setPen(pen);

        // Large circular trackpads (the SC2026 signature feature)
        drawCircle(p, LP_CTR, PAD_R, {0,1,0}, cx, cy, scale, PAD_SEGS);
        drawCircle(p, RP_CTR, PAD_R, {0,1,0}, cx, cy, scale, PAD_SEGS);

        // Thumbsticks
        pen.setWidthF(1.3f);
        p.setPen(pen);
        drawCircle(p, LS_CTR, STICK_R, {0,1,0}, cx, cy, scale, 12);
        drawCircle(p, RS_CTR, STICK_R, {0,1,0}, cx, cy, scale, 12);

        // D-pad cross
        {
            float a = DPAD_ARM, w = DPAD_W;
            auto dc = DPAD_CTR;
            // Vertical arm
            drawLine(p, {dc.x - w, dc.y, dc.z - a}, {dc.x - w, dc.y, dc.z + a}, cx, cy, scale);
            drawLine(p, {dc.x + w, dc.y, dc.z - a}, {dc.x + w, dc.y, dc.z + a}, cx, cy, scale);
            drawLine(p, {dc.x - w, dc.y, dc.z + a}, {dc.x + w, dc.y, dc.z + a}, cx, cy, scale);
            drawLine(p, {dc.x - w, dc.y, dc.z - a}, {dc.x + w, dc.y, dc.z - a}, cx, cy, scale);
            // Horizontal arm
            drawLine(p, {dc.x - a, dc.y, dc.z - w}, {dc.x + a, dc.y, dc.z - w}, cx, cy, scale);
            drawLine(p, {dc.x - a, dc.y, dc.z + w}, {dc.x + a, dc.y, dc.z + w}, cx, cy, scale);
            drawLine(p, {dc.x - a, dc.y, dc.z - w}, {dc.x - a, dc.y, dc.z + w}, cx, cy, scale);
            drawLine(p, {dc.x + a, dc.y, dc.z - w}, {dc.x + a, dc.y, dc.z + w}, cx, cy, scale);
        }

        // ABXY button cluster (4 circles)
        static const float bx[] = { BTN_OFF, -BTN_OFF,  0.f,      0.f     };
        static const float bz[] = { 0.f,      0.f,      BTN_OFF, -BTN_OFF };
        for (int i = 0; i < 4; ++i) {
            V3 bc = { BTN_CTR.x + bx[i], BTN_CTR.y, BTN_CTR.z + bz[i] };
            drawCircle(p, bc, BTN_R, {0,1,0}, cx, cy, scale, 8);
        }

        // Center buttons: Steam (larger), quick-access, view
        drawCircle(p, STEAM_CTR, 0.040f, {0,1,0}, cx, cy, scale, 10);
        drawCircle(p, QAM_CTR,   0.028f, {0,1,0}, cx, cy, scale, 8);
        drawCircle(p, VIEW_CTR,  0.028f, {0,1,0}, cx, cy, scale, 8);
    }

    // ── 3-axis orientation indicator ──────────────────────────────────────────
    // Draws in the top-right corner:
    //   • Horizon chord (blue)  — chord inside the circle, rotated by roll and
    //     shifted vertically by pitch.  Shows roll + pitch.
    //   • Yaw arc (orange) — arc around the outer ring, filling from 12 o'clock
    //     CW or CCW to show accumulated yaw angle.
    {
        const int hx = width()  - 56;  // indicator center X
        const int hy = 46;              // indicator center Y
        const int hr = 34;              // inner circle radius (horizon lives here)
        const int yr = hr + 5;          // yaw arc radius (just outside the circle)

        p.save();
        p.translate(hx, hy);

        // 1. Yaw arc — drawn first so border circle overlaps it cleanly
        float yawDeg = yaw_ * 180.f / float(M_PI);
        // Wrap to (−180, +180]
        while (yawDeg >  180.f) yawDeg -= 360.f;
        while (yawDeg < -180.f) yawDeg += 360.f;

        if (fabsf(yawDeg) > 1.5f) {
            QPen yawPen(QColor(0xff, 0x88, 0x00), 3.0f,
                        Qt::SolidLine, Qt::RoundCap);
            p.setPen(yawPen);
            // Qt drawArc: 0° = 3 o'clock, increases CCW; angles in 1/16 degrees.
            // 12 o'clock = 90°.
            // Positive yaw = CCW from above = CCW arc on screen = positive spanAngle.
            int startAngle16 = 90 * 16;
            int spanAngle16  = int(yawDeg * 16.f);
            p.drawArc(-yr, -yr, yr * 2, yr * 2, startAngle16, spanAngle16);
        }

        // 2. Border circle
        QPen borPen(QColor(0x44, 0x44, 0x66), 1.2f);
        p.setPen(borPen);
        p.drawEllipse(-hr, -hr, hr * 2, hr * 2);

        // 3. Horizon chord
        //    The chord is inside the circle: at vertical offset pitchPx (in rotated
        //    coordinates), rotated by rollDeg.
        float rollDeg  = roll_  * 180.f / float(M_PI);
        // Map pitch: ±90° → ±hr pixels (full range fills the circle)
        float pitchPx  = std::clamp(pitch_ / (float(M_PI) / 2.f) * float(hr),
                                    -float(hr), float(hr));

        p.save();
        p.rotate(rollDeg);  // coordinate system rotates with roll

        // Chord endpoints at height pitchPx within circle of radius hr
        float h2       = float(hr) * float(hr) - pitchPx * pitchPx;
        float chordHalf = (h2 > 0.f) ? sqrtf(h2) : 0.f;

        if (chordHalf > 1.f) {
            // Filled-chord look: draw three parallel lines for thickness variation
            QPen horizPen(QColor(0x44, 0x88, 0xff), 2.2f);
            p.setPen(horizPen);
            p.drawLine(QPointF(-chordHalf, pitchPx), QPointF(chordHalf, pitchPx));

            // Small pitch-indicator tick (perpendicular nub at each end)
            p.drawLine(QPointF(-chordHalf, pitchPx - 4), QPointF(-chordHalf, pitchPx + 4));
            p.drawLine(QPointF( chordHalf, pitchPx - 4), QPointF( chordHalf, pitchPx + 4));
        }
        p.restore();

        // 4. Reference crosshair (fixed, shows horizon datum)
        QPen refPen(QColor(0xff, 0xff, 0xff, 140), 1.0f);
        p.setPen(refPen);
        p.drawLine(-6, 0, 6, 0);
        p.drawLine( 0, -6, 0, 6);

        p.restore();
    }
}

// ── Complementary filter ──────────────────────────────────────────────────────

void ControllerView::pushSample(float accel[3], float gyro[3], quint64 elapsed_us) {
    std::copy(accel, accel + 3, lastAccel_);
    std::copy(gyro,  gyro  + 3, lastGyro_);

    float dt = float(elapsed_us) / 1e6f;
    if (dt <= 0.f || dt > MAX_DT) dt = 0.008f;

    // DSU-space:
    //   accel_x positive → AccelRightToLeft   (right side down)
    //   accel_y positive → AccelFrontToBack   (face down)
    //   accel_z positive → AccelTopToBottom   (flat face-up gives +1g on accel_z)
    //
    // Gravity vector from accelerometer:
    //   roll_acc  = atan2(ax, az)   → tilt left/right (right side down = positive roll)
    //   pitch_acc = atan2(-ay, az)  → tilt nose up/down (face tilts down = positive pitch)

    float norm = sqrtf(accel[0]*accel[0] + accel[1]*accel[1] + accel[2]*accel[2]);
    if (norm < 0.3f || norm > 3.0f) {
        // High motion or sensor dropout — integrate gyro only
        // (Use same axis assignments as the filter branch below)
        float pitch_rate = gyro[0] * float(M_PI) / 180.f;
        float roll_rate  = gyro[2] * float(M_PI) / 180.f;
        float yaw_rate   = -gyro[1] * float(M_PI) / 180.f;
        roll_  += roll_rate  * dt;
        pitch_ += pitch_rate * dt;
        yaw_   += yaw_rate   * dt;
        update();
        return;
    }

    float ax = accel[0] / norm;
    float ay = accel[1] / norm;
    float az = accel[2] / norm;

    float roll_acc  = atan2f( ax,  az);
    float pitch_acc = atan2f(-ay,  az);

    // DSU gyro axis → physical rotation mapping (from DEFAULT_GYRO src assignments):
    //   gyro[0] = raw[0] (side axis)        → rotation around X → pitch rate
    //   gyro[1] = -raw[2] (top-bottom axis) → rotation around Z → −yaw rate (inv_y=true)
    //   gyro[2] = raw[1] (face-depth axis)  → rotation around Y → roll rate
    float pitch_rate = gyro[0] * float(M_PI) / 180.f;
    float roll_rate  = gyro[2] * float(M_PI) / 180.f;
    float yaw_rate   = -gyro[1] * float(M_PI) / 180.f;

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
