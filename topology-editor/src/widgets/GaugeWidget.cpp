#include "widgets/GaugeWidget.h"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QtMath>

namespace {
constexpr double kStartDeg = 210;   // 表盘起点（左下）
constexpr double kSweepDeg = 240;   // 顺时针扫过的角度
const QColor kGreen(0x2e, 0xa0, 0x43);
const QColor kOrange(0xe0, 0x9b, 0x10);
const QColor kRed(0xd9, 0x30, 0x25);

QPointF polar(const QPointF &c, double r, double deg)
{
    const double a = qDegreesToRadians(deg);
    return {c.x() + r * std::cos(a), c.y() - r * std::sin(a)};
}
} // namespace

GaugeWidget::GaugeWidget(QWidget *parent) : QWidget(parent), m_anim(new QTimer(this))
{
    QSizePolicy sp(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
    m_anim->setInterval(16);
    connect(m_anim, &QTimer::timeout, this, &GaugeWidget::animate);
}

void GaugeWidget::setTitle(const QString &title)
{
    m_title = title;
    m_dialDirty = true;
    update();
}

void GaugeWidget::setUnit(const QString &unit)
{
    m_unit = unit;
    update();
}

void GaugeWidget::setRange(double min, double max)
{
    m_min = min;
    m_max = std::max(max, min + 1e-6);
    m_dialDirty = true;
    update();
}

void GaugeWidget::setThresholds(double warn, double alarm)
{
    m_warn = warn;
    m_alarm = alarm;
    m_dialDirty = true;
    update();
}

void GaugeWidget::setValue(double v)
{
    v = std::clamp(v, m_min, m_max);
    if (!m_hasValue) {
        m_hasValue = true;
        m_shown = m_min;   // 从零位转上来
    }
    m_target = v;
    if (!m_anim->isActive())
        m_anim->start();
}

void GaugeWidget::clearValue()
{
    if (!m_hasValue)
        return;
    m_hasValue = false;
    m_anim->stop();
    update();
}

void GaugeWidget::animate()
{
    // 指数缓动：每帧走剩余距离的 20%，接近目标后停表，静止时不占 CPU
    const double diff = m_target - m_shown;
    if (std::abs(diff) < (m_max - m_min) * 0.001) {
        m_shown = m_target;
        m_anim->stop();
    } else {
        m_shown += diff * 0.2;
    }
    update();
}

double GaugeWidget::angleFor(double v) const
{
    const double f = std::clamp((v - m_min) / (m_max - m_min), 0.0, 1.0);
    return kStartDeg - kSweepDeg * f;
}

QColor GaugeWidget::levelColor(double v) const
{
    return v >= m_alarm ? kRed : v >= m_warn ? kOrange : kGreen;
}

void GaugeWidget::resizeEvent(QResizeEvent *)
{
    m_dialDirty = true;
}

void GaugeWidget::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::FontChange)
        m_dialDirty = true;
    QWidget::changeEvent(e);
}

void GaugeWidget::rebuildDial()
{
    const qreal dpr = devicePixelRatioF();
    m_dial = QPixmap(size() * dpr);
    m_dial.setDevicePixelRatio(dpr);
    m_dial.fill(Qt::transparent);

    const double side = std::min(width(), height());
    const QPointF c(width() / 2.0, height() / 2.0 + side * 0.04);
    const double R = side / 2.0 - 4;
    const QPalette pal = palette();
    QColor fg = pal.color(QPalette::WindowText);

    QPainter p(&m_dial);
    p.setRenderHint(QPainter::Antialiasing);

    // 表盘底
    QRadialGradient g(c, R);
    g.setColorAt(0, pal.color(QPalette::Base));
    g.setColorAt(1, pal.color(QPalette::Window));
    p.setBrush(g);
    QColor rim = fg;
    rim.setAlpha(40);
    p.setPen(QPen(rim, 1));
    p.drawEllipse(c, R, R);

    // 三段色带
    const double bandR = R * 0.86;
    const QRectF bandRect(c.x() - bandR, c.y() - bandR, 2 * bandR, 2 * bandR);
    auto band = [&](double from, double to, QColor col) {
        from = std::clamp(from, m_min, m_max);
        to = std::clamp(to, m_min, m_max);
        if (to <= from)
            return;
        col.setAlpha(200);
        p.setPen(QPen(col, side * 0.045, Qt::SolidLine, Qt::FlatCap));
        const double a0 = angleFor(from);
        const double a1 = angleFor(to);
        p.drawArc(bandRect, int(a0 * 16), int((a1 - a0) * 16));
    };
    band(m_min, m_warn, kGreen);
    band(m_warn, m_alarm, kOrange);
    band(m_alarm, m_max, kRed);

    // 刻度：10 个大格，每格 5 个小格
    QColor tick = fg;
    tick.setAlpha(170);
    QFont f = font();
    f.setPixelSize(std::max(8, int(side * 0.065)));
    p.setFont(f);
    for (int i = 0; i <= 50; ++i) {
        const bool major = i % 5 == 0;
        const double a = kStartDeg - kSweepDeg * i / 50.0;
        p.setPen(QPen(tick, major ? 1.6 : 0.8));
        p.drawLine(polar(c, R * 0.79, a), polar(c, R * (major ? 0.68 : 0.73), a));
        if (major) {
            const double v = m_min + (m_max - m_min) * i / 50.0;
            const QPointF at = polar(c, R * 0.55, a);
            p.drawText(QRectF(at.x() - 24, at.y() - 8, 48, 16), Qt::AlignCenter, QString::number(v, 'g', 4));
        }
    }

    // 标题
    f.setPixelSize(std::max(9, int(side * 0.075)));
    p.setFont(f);
    p.setPen(fg);
    p.drawText(QRectF(c.x() - R, c.y() + R * 0.52, 2 * R, R * 0.22), Qt::AlignCenter, m_title);

    m_dialDirty = false;
}

void GaugeWidget::paintEvent(QPaintEvent *)
{
    if (m_dialDirty || m_dial.size() != size() * devicePixelRatioF())
        rebuildDial();

    QPainter p(this);
    p.drawPixmap(0, 0, m_dial);
    p.setRenderHint(QPainter::Antialiasing);

    const double side = std::min(width(), height());
    const QPointF c(width() / 2.0, height() / 2.0 + side * 0.04);
    const double R = side / 2.0 - 4;
    const QColor gray(0x8a, 0x8f, 0x98);
    const QColor col = m_hasValue ? levelColor(m_shown) : gray;

    // 读数
    QFont f = font();
    f.setPixelSize(std::max(10, int(side * 0.15)));
    f.setBold(true);
    p.setFont(f);
    p.setPen(col);
    const QString text = m_hasValue ? QString::number(m_shown, 'f', m_decimals) : QStringLiteral("--");
    p.drawText(QRectF(c.x() - R, c.y() + R * 0.14, 2 * R, R * 0.26), Qt::AlignCenter, text);
    f.setBold(false);
    f.setPixelSize(std::max(8, int(side * 0.065)));
    p.setFont(f);
    p.setPen(gray);
    p.drawText(QRectF(c.x() - R, c.y() + R * 0.38, 2 * R, R * 0.16), Qt::AlignCenter, m_unit);

    // 指针：细长三角 + 尾巴
    const double a = angleFor(m_hasValue ? m_shown : m_min);
    const double w = side * 0.02;
    QPainterPath needle;
    needle.moveTo(polar(c, R * 0.74, a));
    needle.lineTo(polar(c, w, a + 90));
    needle.lineTo(polar(c, R * 0.14, a + 180));
    needle.lineTo(polar(c, w, a - 90));
    needle.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawPath(needle);
    p.setBrush(palette().color(QPalette::Base));
    p.setPen(QPen(col, 2));
    p.drawEllipse(c, side * 0.035, side * 0.035);
}
