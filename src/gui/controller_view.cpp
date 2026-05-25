#include "controller_view.h"
#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <cmath>
#include <algorithm>

namespace sc2 {

// ── Complementary filter tuning ───────────────────────────────────────────────
static constexpr float ALPHA  = 0.96f;
static constexpr float MAX_DT = 0.05f;

// ── SC2026 body geometry ──────────────────────────────────────────────────────
//
// The 2026 Steam Controller uses a conventional Xbox-like gamepad silhouette:
// wide shoulder area at the top, slight waist taper, two grip prongs at the
// bottom.  Haptic touchpads are small and square, located BELOW the thumbsticks
// (not large circles at the top like the original SC).
//
// Model space: X = right, Y = toward camera (face), Z = up on screen.
// Scale: 1.0 ≈ half the controller width.  Front face at Y = +0.10.
//
// 20-vertex outline, clockwise from top-left:
static const ControllerView::V3 BODY[] = {
    {-0.70f,  0.10f,  0.52f}, //  0  top-left shoulder
    {-0.34f,  0.10f,  0.60f}, //  1  top center-left
    { 0.34f,  0.10f,  0.60f}, //  2  top center-right
    { 0.70f,  0.10f,  0.52f}, //  3  top-right shoulder
    { 0.88f,  0.10f,  0.30f}, //  4  right upper arc
    { 0.92f,  0.10f,  0.02f}, //  5  right side (widest)
    { 0.84f,  0.10f, -0.16f}, //  6  right waist
    { 0.66f,  0.10f, -0.30f}, //  7  right waist lower
    { 0.64f,  0.10f, -0.66f}, //  8  right grip outer
    { 0.48f,  0.10f, -0.86f}, //  9  right grip bottom-outer
    { 0.28f,  0.10f, -0.93f}, // 10  right grip bottom
    { 0.10f,  0.10f, -0.86f}, // 11  right grip inner
    {-0.10f,  0.10f, -0.86f}, // 12  left grip inner
    {-0.28f,  0.10f, -0.93f}, // 13  left grip bottom
    {-0.48f,  0.10f, -0.86f}, // 14  left grip bottom-outer
    {-0.64f,  0.10f, -0.66f}, // 15  left grip outer
    {-0.66f,  0.10f, -0.30f}, // 16  left waist lower
    {-0.84f,  0.10f, -0.16f}, // 17  left waist
    {-0.92f,  0.10f,  0.02f}, // 18  left side (widest)
    {-0.88f,  0.10f,  0.30f}, // 19  left upper arc
};
static const int BODY_N = 20;
static const float FRONT_Y =  0.10f;
static const float BACK_Y  = -0.10f;

static ControllerView::V3 backV(ControllerView::V3 v) { return {v.x, BACK_Y, v.z}; }

// ── Bumper / shoulder button outlines ─────────────────────────────────────────
// Thin band along the top of each shoulder (front face only).
static const ControllerView::V3 LBUMP[4] = {
    {-0.90f, FRONT_Y, 0.52f}, {-0.38f, FRONT_Y, 0.62f},
    {-0.38f, FRONT_Y, 0.52f}, {-0.90f, FRONT_Y, 0.44f},
};
static const ControllerView::V3 RBUMP[4] = {
    { 0.38f, FRONT_Y, 0.62f}, { 0.90f, FRONT_Y, 0.52f},
    { 0.90f, FRONT_Y, 0.44f}, { 0.38f, FRONT_Y, 0.52f},
};

// ── Thumbsticks ───────────────────────────────────────────────────────────────
static const ControllerView::V3 LS_CTR = {-0.34f, FRONT_Y,  0.06f};
static const ControllerView::V3 RS_CTR = { 0.34f, FRONT_Y,  0.06f};
static constexpr float STICK_R = 0.11f;

// ── D-pad (left upper area) ───────────────────────────────────────────────────
static const ControllerView::V3 DPAD_CTR = {-0.58f, FRONT_Y, 0.32f};
static constexpr float DPAD_ARM = 0.070f;
static constexpr float DPAD_W   = 0.028f;

// ── ABXY face buttons (right upper area) ─────────────────────────────────────
static const ControllerView::V3 BTN_CTR = { 0.58f, FRONT_Y, 0.32f};
static constexpr float BTN_R   = 0.048f;
static constexpr float BTN_OFF = 0.086f;

// ── Square haptic touchpads (below thumbsticks, angled slightly inward) ───────
static const ControllerView::V3 LP_CTR = {-0.52f, FRONT_Y, -0.34f};
static const ControllerView::V3 RP_CTR = { 0.52f, FRONT_Y, -0.34f};
static constexpr float PAD_HW = 0.18f;   // half-width  (X)
static constexpr float PAD_HH = 0.14f;   // half-height (Z)

// ── Center cluster ────────────────────────────────────────────────────────────
static const ControllerView::V3 STEAM_CTR = { 0.00f, FRONT_Y,  0.18f};
static const ControllerView::V3 BACK_CTR  = {-0.18f, FRONT_Y,  0.06f};
static const ControllerView::V3 MENU_CTR  = { 0.18f, FRONT_Y,  0.06f};
static const ControllerView::V3 QAM_CTR   = { 0.00f, FRONT_Y,  0.06f};

// ── Back grip buttons (2 per side, on back face) ─────────────────────────────
static const ControllerView::V3 GRIP_BTNS[] = {
    {-0.70f, BACK_Y, -0.42f}, {-0.70f, BACK_Y, -0.58f},   // left grip: L4, L5
    { 0.70f, BACK_Y, -0.42f}, { 0.70f, BACK_Y, -0.58f},   // right grip: R4, R5
};
static constexpr float GRIP_BTN_R = 0.042f;

// ─────────────────────────────────────────────────────────────────────────────

ControllerView::ControllerView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(200, 160);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x12, 0x12, 0x20));
    setPalette(pal);
    setAutoFillBackground(true);
}

void ControllerView::resetFilter() {
    roll_ = pitch_ = yaw_ = 0.f;
    filterInit_ = false;
}

// ── Math helpers ──────────────────────────────────────────────────────────────

ControllerView::V3 ControllerView::rotate(V3 v) const {
    // 1. Pitch around X
    float cp = cosf(pitch_), sp = sinf(pitch_);
    float y1 =  cp * v.y - sp * v.z,  z1 =  sp * v.y + cp * v.z,  x1 = v.x;
    // 2. Roll around Y
    float cr = cosf(roll_),  sr = sinf(roll_);
    float x2 =  cr * x1 + sr * z1,  z2 = -sr * x1 + cr * z1,  y2 = y1;
    // 3. Yaw around Z
    float cy = cosf(yaw_),   sy = sinf(yaw_);
    return { cy * x2 - sy * y2,  sy * x2 + cy * y2,  z2 };
}

QPointF ControllerView::project(V3 v, float cx, float cy, float scale) const {
    const float camDist = 3.5f;
    float w = camDist - v.y;
    return { cx + scale * v.x / w,  cy - scale * v.z / w };
}

void ControllerView::drawLine(QPainter& p, V3 a, V3 b,
                               float cx, float cy, float scale) const {
    p.drawLine(project(rotate(a), cx, cy, scale),
               project(rotate(b), cx, cy, scale));
}

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

// Square haptic pad: outline + small inner cross to indicate it's a touchpad.
void ControllerView::drawSquarePad(QPainter& p, V3 ctr, float hw, float hh,
                                    float cx, float cy, float scale) const {
    V3 corners[4] = {
        {ctr.x - hw, ctr.y, ctr.z + hh},
        {ctr.x + hw, ctr.y, ctr.z + hh},
        {ctr.x + hw, ctr.y, ctr.z - hh},
        {ctr.x - hw, ctr.y, ctr.z - hh},
    };
    QPolygonF poly;
    for (auto& c : corners) poly << project(rotate(c), cx, cy, scale);
    poly << poly[0];
    p.drawPolyline(poly);

    // Inner cross
    drawLine(p, {ctr.x - hw * 0.35f, ctr.y, ctr.z},
                {ctr.x + hw * 0.35f, ctr.y, ctr.z}, cx, cy, scale);
    drawLine(p, {ctr.x, ctr.y, ctr.z - hh * 0.35f},
                {ctr.x, ctr.y, ctr.z + hh * 0.35f}, cx, cy, scale);
}

// D-pad cross as a proper plus-sign outline.
void ControllerView::drawDpad(QPainter& p, V3 ctr, float arm, float w,
                               float cx, float cy, float scale) const {
    float x = ctr.x, y = ctr.y, z = ctr.z;
    // Horizontal arm
    drawLine(p, {x-arm, y, z-w}, {x-arm, y, z+w}, cx, cy, scale);
    drawLine(p, {x+arm, y, z-w}, {x+arm, y, z+w}, cx, cy, scale);
    drawLine(p, {x-arm, y, z+w}, {x-w,   y, z+w}, cx, cy, scale);
    drawLine(p, {x+arm, y, z+w}, {x+w,   y, z+w}, cx, cy, scale);
    drawLine(p, {x-arm, y, z-w}, {x-w,   y, z-w}, cx, cy, scale);
    drawLine(p, {x+arm, y, z-w}, {x+w,   y, z-w}, cx, cy, scale);
    // Vertical arm
    drawLine(p, {x-w, y, z-arm}, {x-w, y, z-w}, cx, cy, scale);
    drawLine(p, {x+w, y, z-arm}, {x+w, y, z-w}, cx, cy, scale);
    drawLine(p, {x-w, y, z+arm}, {x-w, y, z+w}, cx, cy, scale);
    drawLine(p, {x+w, y, z+arm}, {x+w, y, z+w}, cx, cy, scale);
    drawLine(p, {x-w, y, z-arm}, {x+w, y, z-arm}, cx, cy, scale);
    drawLine(p, {x-w, y, z+arm}, {x+w, y, z+arm}, cx, cy, scale);
}

// Build a QPainterPath for the front or back body outline (projected).
void ControllerView::buildBodyPath(QPainterPath& path, float cx, float cy,
                                    float scale, bool front) const {
    path = QPainterPath();
    for (int i = 0; i < BODY_N; ++i) {
        V3 v = front ? BODY[i] : backV(BODY[i]);
        QPointF pt = project(rotate(v), cx, cy, scale);
        if (i == 0) path.moveTo(pt);
        else         path.lineTo(pt);
    }
    path.closeSubpath();
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void ControllerView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const float cx    = width()  * 0.50f;
    const float cy    = height() * 0.52f;
    const float scale = std::min(width(), height()) * 0.40f;

    // Face normal — determines which face is toward the camera.
    V3 faceNorm  = rotate({0.f, 1.f, 0.f});
    bool faceFwd = (faceNorm.y > 0.f);

    // ── Colour palette ────────────────────────────────────────────────────────
    QColor cFront(0x00, 0xe8, 0x80);     // bright green — front face outline
    QColor cBack (0x00, 0x72, 0x40);     // dim green    — back face outline
    QColor cEdge (0x00, 0xaa, 0x58);     // medium       — connecting edges
    QColor cFill (0x1c, 0x1c, 0x30);     // body fill (front)
    QColor cFillB(0x14, 0x14, 0x22);     // body fill (back)
    QColor cFeat (faceFwd ? cFront : cBack); // feature colour matches visible face

    // ── 1. Back face (filled + outline) ──────────────────────────────────────
    {
        QPainterPath backPath;
        buildBodyPath(backPath, cx, cy, scale, false);
        p.fillPath(backPath, faceFwd ? cFillB : cFill);
        QPen pen(faceFwd ? cBack : cFront, 1.2f);
        p.setPen(pen);
        p.drawPath(backPath);

        // Back grip buttons (only visible when back faces camera)
        if (!faceFwd) {
            QPen gbPen(cFront, 1.2f);
            p.setPen(gbPen);
            for (const auto& gb : GRIP_BTNS)
                drawCircle(p, gb, GRIP_BTN_R, {0,1,0}, cx, cy, scale, 8);
        }
    }

    // ── 2. Connecting edges (front ↔ back) ────────────────────────────────────
    {
        QPen pen(cEdge, 1.0f);
        p.setPen(pen);
        const int edges[] = {0,3,4,5,7,8,10,11,12,13,15,16,17,18,19};
        for (int i : edges)
            drawLine(p, BODY[i], backV(BODY[i]), cx, cy, scale);
    }

    // ── 3. Front face (filled + outline) ──────────────────────────────────────
    {
        QPainterPath frontPath;
        buildBodyPath(frontPath, cx, cy, scale, true);
        p.fillPath(frontPath, faceFwd ? cFill : cFillB);
        QPen pen(faceFwd ? cFront : cBack, 1.8f);
        p.setPen(pen);
        p.drawPath(frontPath);
    }

    // ── 4. Front-face features ────────────────────────────────────────────────
    {
        float alpha = faceFwd ? 0.95f : 0.35f;
        cFeat.setAlphaF(alpha);

        // Shoulder / bumper outlines
        {
            QPen pen(cFeat, 1.3f);
            p.setPen(pen);
            // Left bumper
            QPolygonF lb;
            for (const auto& v : LBUMP) lb << project(rotate(v), cx, cy, scale);
            lb << lb[0];
            p.drawPolyline(lb);
            // Right bumper
            QPolygonF rb;
            for (const auto& v : RBUMP) rb << project(rotate(v), cx, cy, scale);
            rb << rb[0];
            p.drawPolyline(rb);
        }

        // Thumbsticks
        {
            QPen pen(cFeat, 1.4f);
            p.setPen(pen);
            drawCircle(p, LS_CTR, STICK_R, {0,1,0}, cx, cy, scale, 16);
            drawCircle(p, RS_CTR, STICK_R, {0,1,0}, cx, cy, scale, 16);
        }

        // D-pad
        {
            QPen pen(cFeat, 1.2f);
            p.setPen(pen);
            drawDpad(p, DPAD_CTR, DPAD_ARM, DPAD_W, cx, cy, scale);
        }

        // ABXY buttons (4 circles in a diamond)
        {
            QPen pen(cFeat, 1.2f);
            p.setPen(pen);
            static const float bx[] = { BTN_OFF, -BTN_OFF,  0.f,      0.f     };
            static const float bz[] = { 0.f,      0.f,      BTN_OFF, -BTN_OFF };
            for (int i = 0; i < 4; ++i) {
                V3 bc = { BTN_CTR.x + bx[i], BTN_CTR.y, BTN_CTR.z + bz[i] };
                drawCircle(p, bc, BTN_R, {0,1,0}, cx, cy, scale, 8);
            }
        }

        // Square haptic touchpads
        {
            QPen pen(cFeat, 1.4f);
            p.setPen(pen);
            drawSquarePad(p, LP_CTR, PAD_HW, PAD_HH, cx, cy, scale);
            drawSquarePad(p, RP_CTR, PAD_HW, PAD_HH, cx, cy, scale);
        }

        // Center cluster: Steam (larger), Back, Menu, QAM
        {
            QPen pen(cFeat, 1.1f);
            p.setPen(pen);
            drawCircle(p, STEAM_CTR, 0.044f, {0,1,0}, cx, cy, scale, 12);
            drawCircle(p, BACK_CTR,  0.026f, {0,1,0}, cx, cy, scale, 8);
            drawCircle(p, MENU_CTR,  0.026f, {0,1,0}, cx, cy, scale, 8);
            drawCircle(p, QAM_CTR,   0.026f, {0,1,0}, cx, cy, scale, 8);
        }

        // Back grip buttons shown as small dots on the grip edges (front view hint)
        if (faceFwd) {
            QPen pen(cFeat, 1.0f);
            pen.setStyle(Qt::DotLine);
            p.setPen(pen);
            // Hint positions on front face near grip inner edges
            static const V3 hintPos[] = {
                {-0.12f, FRONT_Y, -0.50f}, {-0.12f, FRONT_Y, -0.64f},
                { 0.12f, FRONT_Y, -0.50f}, { 0.12f, FRONT_Y, -0.64f},
            };
            pen.setStyle(Qt::SolidLine);
            p.setPen(pen);
            for (const auto& h : hintPos)
                drawCircle(p, h, 0.030f, {0,1,0}, cx, cy, scale, 6);
        }
    }

    // ── 5. Orientation indicator (top-right corner) ───────────────────────────
    {
        const int hx = width()  - 56;
        const int hy = 46;
        const int hr = 34;
        const int yr = hr + 5;

        p.save();
        p.translate(hx, hy);

        // Yaw arc (orange, outer ring)
        float yawDeg = yaw_ * 180.f / float(M_PI);
        while (yawDeg >  180.f) yawDeg -= 360.f;
        while (yawDeg < -180.f) yawDeg += 360.f;
        if (fabsf(yawDeg) > 1.5f) {
            QPen yawPen(QColor(0xff, 0x88, 0x00), 3.0f,
                        Qt::SolidLine, Qt::RoundCap);
            p.setPen(yawPen);
            p.drawArc(-yr, -yr, yr * 2, yr * 2,
                      90 * 16, int(yawDeg * 16.f));
        }

        // Border circle
        p.setPen(QPen(QColor(0x44, 0x44, 0x66), 1.2f));
        p.drawEllipse(-hr, -hr, hr * 2, hr * 2);

        // Horizon chord (roll + pitch)
        float rollDeg = roll_  * 180.f / float(M_PI);
        float pitchPx = std::clamp(pitch_ / (float(M_PI) / 2.f) * float(hr),
                                   -float(hr), float(hr));
        p.save();
        p.rotate(rollDeg);
        float h2 = float(hr)*float(hr) - pitchPx*pitchPx;
        float half = (h2 > 0.f) ? sqrtf(h2) : 0.f;
        if (half > 1.f) {
            p.setPen(QPen(QColor(0x44, 0x88, 0xff), 2.2f));
            p.drawLine(QPointF(-half, pitchPx), QPointF(half, pitchPx));
            p.drawLine(QPointF(-half, pitchPx-4), QPointF(-half, pitchPx+4));
            p.drawLine(QPointF( half, pitchPx-4), QPointF( half, pitchPx+4));
        }
        p.restore();

        // Reference crosshair
        p.setPen(QPen(QColor(0xff, 0xff, 0xff, 140), 1.0f));
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

    float norm = sqrtf(accel[0]*accel[0] + accel[1]*accel[1] + accel[2]*accel[2]);
    if (norm < 0.3f || norm > 3.0f) {
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

    // DSU convention: gyro[0]=pitch, gyro[1]=yaw (inv), gyro[2]=roll
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
