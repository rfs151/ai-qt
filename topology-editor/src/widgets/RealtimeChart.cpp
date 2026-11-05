#include "widgets/RealtimeChart.h"
#include "comm/DeviceSim.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

namespace {

constexpr int kLeft = 54;
constexpr int kRight = 12;
constexpr int kTop = 26;
constexpr int kBottom = 22;
constexpr int kLaneGap = 12;

double niceStep(double range, int ticks)
{
    const double raw = range / std::max(1, ticks);
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    const double step = norm < 1.5 ? 1 : norm < 3 ? 2 : norm < 7 ? 5 : 10;
    return step * mag;
}

qint64 timeStep(qint64 spanMs, int maxTicks)
{
    static const qint64 steps[] = {10,    20,     50,     100,    200,     500,     1000,    2000,
                                   5000,  10000,  15000,  30000,  60000,   120000,  300000,  600000,
                                   900000, 1800000, 3600000, 7200000, 21600000, 43200000, 86400000};
    for (qint64 s : steps)
        if (spanMs / s <= maxTicks)
            return s;
    return 86400000;
}

} // namespace

RealtimeChart::RealtimeChart(QWidget *parent) : QWidget(parent), m_frameTimer(new QTimer(this))
{
    setMouseTracking(true);
    setMinimumHeight(160);
    setAttribute(Qt::WA_OpaquePaintEvent);   // 每次都整块重绘，省掉 Qt 先擦背景的一步
    m_frameTimer->setInterval(33);
    connect(m_frameTimer, &QTimer::timeout, this, &RealtimeChart::onFrame);
    m_frameTimer->start();
}

void RealtimeChart::setLanes(const QVector<Lane> &lanes)
{
    m_lanes = lanes;
    int maxIndex = 0;
    for (const Lane &l : lanes)
        for (const Channel &c : l.channels)
            maxIndex = std::max(maxIndex, c.index);
    m_hidden = QVector<bool>(maxIndex + 1, false);
    update();
}

void RealtimeChart::setSeries(int deviceId, const QString &title)
{
    m_deviceId = deviceId;
    m_title = title;
    update();
}

void RealtimeChart::setSpanSeconds(double seconds)
{
    m_spanMs = seconds * 1000;
    setPaused(false);
    update();
}

void RealtimeChart::showRange(qint64 t0, qint64 t1)
{
    m_home = {t0, std::max(t1, t0 + 1000)};
    freezeAt(m_home);
    update();
}

void RealtimeChart::setPaused(bool paused)
{
    if (paused == m_paused || (!paused && !m_liveEnabled))
        return;
    if (paused) {
        freezeAt(currentView(m_store ? m_store->series(m_deviceId) : nullptr));
    } else {
        m_paused = false;
        emit pausedChanged(false);
    }
    update();
}

void RealtimeChart::freezeAt(const View &v)
{
    m_viewEnd = v.t1;
    m_pausedSpanMs = double(v.t1 - v.t0);
    if (!m_paused) {
        m_paused = true;
        emit pausedChanged(true);
    }
}

RealtimeChart::View RealtimeChart::currentView(const TelemetrySeries *s) const
{
    if (m_paused)
        return {m_viewEnd - qint64(m_pausedSpanMs), m_viewEnd};
    const qint64 now = Clock::nowMs();
    if (m_spanMs > 0)
        return {now - qint64(m_spanMs), now};
    // 全部：从最早的点到现在
    const qint64 first = (s && !s->isEmpty()) ? s->at(0).t : now - 60000;
    const qint64 last = (s && !s->isEmpty()) ? std::max(now, s->back().t) : now;
    return {first, std::max(last, first + 1000)};
}

QRect RealtimeChart::plotArea() const
{
    return rect().adjusted(kLeft, kTop, -kRight, -kBottom);
}

QRect RealtimeChart::laneRect(int lane) const
{
    const QRect plot = plotArea();
    const int n = std::max<int>(1, int(m_lanes.size()));
    const int h = (plot.height() - kLaneGap * (n - 1)) / n;
    return QRect(plot.left(), plot.top() + lane * (h + kLaneGap), plot.width(), h);
}

void RealtimeChart::onFrame()
{
    if (!isVisible())
        return;
    // 暂停且数据没变时不重绘；实时模式下时间轴在走，每帧都要画
    if (m_paused && m_store && m_store->version() == m_drawnVersion)
        return;
    update();
}

// ------------------------------------------------------------------ 绘制

void RealtimeChart::paintEvent(QPaintEvent *)
{
    QElapsedTimer timer;
    timer.start();

    QPainter p(this);
    const QPalette pal = palette();
    const QColor bg = pal.color(QPalette::Base);
    const QColor fg = pal.color(QPalette::WindowText);
    QColor grid = fg;
    grid.setAlpha(28);
    QColor faint = fg;
    faint.setAlpha(140);
    p.fillRect(rect(), bg);

    const TelemetrySeries *s = m_store ? m_store->series(m_deviceId) : nullptr;
    if (m_store)
        m_drawnVersion = m_store->version();
    const View v = currentView(s);
    const QRect plot = plotArea();
    if (plot.width() < 20 || plot.height() < 20)
        return;
    const double span = double(v.t1 - v.t0);
    auto xOfT = [&](qint64 t) { return plot.left() + double(t - v.t0) / span * plot.width(); };

    // ---- 标题和图例
    QFont small = font();
    small.setPixelSize(11);
    p.setFont(font());
    p.setPen(fg);
    p.drawText(QRect(kLeft, 4, plot.width() / 2, kTop - 6), Qt::AlignLeft | Qt::AlignVCenter,
               m_title.isEmpty() ? QStringLiteral("未选择设备") : m_title);
    p.setFont(small);
    m_legendHits.clear();
    {
        int x = plot.right();
        for (int li = int(m_lanes.size()) - 1; li >= 0; --li) {
            for (int ci = int(m_lanes[li].channels.size()) - 1; ci >= 0; --ci) {
                const Channel &c = m_lanes[li].channels[ci];
                const int w = p.fontMetrics().horizontalAdvance(c.name) + 22;
                x -= w;
                const QRect hit(x, 4, w - 6, kTop - 8);
                const bool hidden = m_hidden.value(c.index);
                p.fillRect(QRect(hit.left(), hit.center().y() - 4, 10, 8), hidden ? grid : c.color);
                p.setPen(hidden ? grid : faint);
                p.drawText(hit.adjusted(14, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, c.name);
                m_legendHits.append({hit, c.index});
            }
        }
    }

    // ---- 时间轴
    const qint64 tStep = timeStep(qint64(span), std::max(2, plot.width() / 90));
    const qint64 monoNow = Clock::nowMs();
    const qint64 wallNow = QDateTime::currentMSecsSinceEpoch();
    const QString fmt = tStep >= 60000 ? QStringLiteral("HH:mm") : tStep >= 1000 ? QStringLiteral("HH:mm:ss")
                                                                                   : QStringLiteral("mm:ss.zzz");
    p.setFont(small);
    for (qint64 t = (v.t0 / tStep + 1) * tStep; t < v.t1; t += tStep) {
        const double x = xOfT(t);
        p.setPen(grid);
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        p.setPen(faint);
        const QString label = QDateTime::fromMSecsSinceEpoch(wallNow - (monoNow - t)).toString(fmt);
        p.drawText(QRectF(x - 40, plot.bottom() + 3, 80, kBottom - 4), Qt::AlignHCenter | Qt::AlignTop, label);
    }

    // ---- 各分区
    // 断线判定：超过 3 个采样周期（至少 1.5 秒）没有数据就断开折线
    qint64 gapMs = 1500;
    if (s && s->size() >= 2)
        gapMs = std::max<qint64>(1500, 3 * (s->back().t - s->at(s->size() - 2).t));

    qsizetype totalIn = 0, totalOut = 0;
    struct Drawn { const Channel *ch; QRect lane; double yMin, yMax; };
    QVector<Drawn> drawn;

    for (int li = 0; li < m_lanes.size(); ++li) {
        const Lane &lane = m_lanes[li];
        const QRect r = laneRect(li);

        QVector<QPair<const Channel *, DecimateResult>> results;
        double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
        if (s) {
            for (const Channel &c : lane.channels) {
                if (m_hidden.value(c.index))
                    continue;
                DecimateResult d = decimateMinMax(*s, c.index, v.t0, v.t1, r.width(), gapMs);
                totalIn = std::max(totalIn, d.inputPoints);
                totalOut += d.outputPoints;
                if (d.outputPoints > 0) {
                    lo = std::min(lo, d.minY);
                    hi = std::max(hi, d.maxY);
                }
                results.append({&c, std::move(d)});
            }
        }

        double yMin = lane.yMin, yMax = lane.yMax;
        if (lane.autoScale && lo <= hi) {
            const double pad = std::max((hi - lo) * 0.1, 1.0);
            const double step = niceStep(hi - lo + 2 * pad, 4);
            yMin = std::floor((lo - pad) / step) * step;
            yMax = std::ceil((hi + pad) / step) * step;
            if (lane.yMin >= 0)
                yMin = std::max(0.0, yMin);
        }
        auto yOf = [&](double val) { return r.bottom() - (val - yMin) / (yMax - yMin) * r.height(); };

        // 纵轴网格和刻度
        p.setFont(small);
        const double yStep = niceStep(yMax - yMin, std::max(2, r.height() / 32));
        for (double y = std::ceil(yMin / yStep) * yStep; y <= yMax + 1e-9; y += yStep) {
            const double py = yOf(y);
            p.setPen(grid);
            p.drawLine(QPointF(r.left(), py), QPointF(r.right(), py));
            p.setPen(faint);
            p.drawText(QRectF(0, py - 8, kLeft - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(y, 'g', 5));
        }
        // 单位写在分区左上角内侧，不和纵轴刻度挤在一起
        p.setPen(faint);
        p.drawText(QRectF(r.left() + 6, r.top() + 2, 120, 14), Qt::AlignLeft | Qt::AlignTop, lane.unit);
        p.setPen(grid);
        p.drawRect(r.adjusted(0, 0, -1, -1));

        // 阈值参考线
        QPen markerPen(QColor(217, 48, 37, 120), 1, Qt::DashLine);
        for (double m : lane.markers) {
            if (m <= yMin || m >= yMax)
                continue;
            p.setPen(markerPen);
            p.drawLine(QPointF(r.left(), yOf(m)), QPointF(r.right(), yOf(m)));
        }

        // 曲线
        p.save();
        p.setClipRect(r.adjusted(0, 0, 1, 1));
        p.setRenderHint(QPainter::Antialiasing);
        for (const auto &[ch, d] : results) {
            p.setPen(QPen(ch->color, 1.5));
            for (const QVector<QPointF> &seg : d.segments) {
                QPolygonF poly;
                poly.reserve(seg.size());
                for (const QPointF &pt : seg)
                    poly.append(QPointF(r.left() + pt.x(), yOf(pt.y())));
                if (poly.size() == 1)
                    p.drawPoint(poly.first());
                else
                    p.drawPolyline(poly);
            }
            drawn.append({ch, r, yMin, yMax});
        }
        p.restore();
    }

    // ---- 十字游标
    if (s && !s->isEmpty() && m_cursorX >= plot.left() && m_cursorX <= plot.right()) {
        const qint64 t = v.t0 + qint64(double(m_cursorX - plot.left()) / plot.width() * span);
        qsizetype i = s->lowerBound(t, [](const TelemetryPoint &pt) { return pt.t; });
        if (i >= s->size())
            i = s->size() - 1;
        if (i > 0 && std::abs(s->at(i - 1).t - t) < std::abs(s->at(i).t - t))
            --i;
        const TelemetryPoint &pt = s->at(i);
        if (std::abs(pt.t - t) <= gapMs) {
            const double x = xOfT(pt.t);
            p.setPen(QPen(faint, 1, Qt::DotLine));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));

            QStringList lines;
            lines << QDateTime::fromMSecsSinceEpoch(wallNow - (monoNow - pt.t)).toString(QStringLiteral("HH:mm:ss.zzz"));
            p.setRenderHint(QPainter::Antialiasing);
            for (const Drawn &d : drawn) {
                const double val = pt.v[size_t(d.ch->index)];
                const double y = d.lane.bottom() - (val - d.yMin) / (d.yMax - d.yMin) * d.lane.height();
                p.setPen(Qt::NoPen);
                p.setBrush(d.ch->color);
                p.drawEllipse(QPointF(x, std::clamp(y, double(d.lane.top()), double(d.lane.bottom()))), 3, 3);
                lines << QStringLiteral("%1  %2").arg(d.ch->name).arg(val, 0, 'f', 1);
            }
            const QFontMetrics fm(small);
            int w = 0;
            for (const QString &l : lines)
                w = std::max(w, fm.horizontalAdvance(l));
            QRect box(0, 0, w + 16, int(lines.size()) * (fm.height() + 1) + 8);
            box.moveTopLeft(QPoint(int(x) + 12, plot.top() + 6));
            if (box.right() > plot.right())
                box.moveRight(int(x) - 12);
            QColor boxBg = bg;
            boxBg.setAlpha(230);
            p.setBrush(boxBg);
            p.setPen(grid);
            p.drawRoundedRect(box, 4, 4);
            p.setPen(fg);
            for (int k = 0; k < lines.size(); ++k)
                p.drawText(box.left() + 8, box.top() + 4 + fm.ascent() + k * (fm.height() + 1), lines[k]);
            p.setBrush(Qt::NoBrush);
        }
    }

    // ---- 状态提示
    p.setFont(small);
    if (!s) {
        p.setPen(faint);
        p.drawText(plot, Qt::AlignCenter, m_emptyText);
    }
    if (m_paused) {
        const QString tip = m_liveEnabled ? QStringLiteral("已暂停 · 滚轮缩放 · 拖动平移 · 双击回到实时")
                                          : QStringLiteral("历史回放 · 滚轮缩放 · 拖动平移 · 双击复位");
        const int w = p.fontMetrics().horizontalAdvance(tip) + 16;
        const QRect r(plot.center().x() - w / 2, plot.top() + 4, w, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(224, 155, 16, 220));
        p.drawRoundedRect(r, 9, 9);
        p.setPen(Qt::white);
        p.drawText(r, Qt::AlignCenter, tip);
        p.setBrush(Qt::NoBrush);
    }
    m_lastIn = totalIn;
    m_lastOut = totalOut;
    p.setPen(faint);
    p.drawText(plot.adjusted(0, 0, -6, -4), Qt::AlignRight | Qt::AlignBottom,
               QStringLiteral("可见 %1 点 → 绘制 %2 点 · %3 ms").arg(totalIn).arg(totalOut).arg(m_paintMs, 0, 'f', 2));

    // 指数平滑，读数不会乱跳
    m_paintMs = m_paintMs * 0.9 + timer.nsecsElapsed() / 1e6 * 0.1;
}

// ------------------------------------------------------------------ 交互

void RealtimeChart::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    for (const auto &[r, index] : std::as_const(m_legendHits)) {
        if (r.contains(e->position().toPoint())) {
            m_hidden[index] = !m_hidden[index];
            update();
            return;
        }
    }
    m_dragStartX = int(e->position().x());
    m_dragStartEnd = currentView(m_store ? m_store->series(m_deviceId) : nullptr).t1;
}

void RealtimeChart::mouseMoveEvent(QMouseEvent *e)
{
    m_cursorX = int(e->position().x());
    if (m_dragStartX >= 0 && (e->buttons() & Qt::LeftButton)) {
        const int dx = m_cursorX - m_dragStartX;
        if (std::abs(dx) > 2 || m_paused) {
            if (!m_paused)
                freezeAt(currentView(m_store ? m_store->series(m_deviceId) : nullptr));
            m_viewEnd = m_dragStartEnd - qint64(double(dx) / plotArea().width() * m_pausedSpanMs);
        }
    }
    update();
}

void RealtimeChart::mouseReleaseEvent(QMouseEvent *)
{
    m_dragStartX = -1;
}

void RealtimeChart::mouseDoubleClickEvent(QMouseEvent *)
{
    if (m_liveEnabled) {
        setPaused(false);
    } else {
        freezeAt(m_home);
        update();
    }
}

void RealtimeChart::wheelEvent(QWheelEvent *e)
{
    const View v = currentView(m_store ? m_store->series(m_deviceId) : nullptr);
    if (!m_paused)
        freezeAt(v);
    const QRect plot = plotArea();
    const double frac = std::clamp(double(e->position().x() - plot.left()) / plot.width(), 0.0, 1.0);
    const double tc = m_viewEnd - m_pausedSpanMs * (1 - frac);   // 鼠标所指的时刻保持不动
    const double factor = std::pow(0.85, e->angleDelta().y() / 120.0);
    const double newSpan = std::clamp(m_pausedSpanMs * factor, 200.0, 7 * 86400000.0);
    m_viewEnd = qint64(tc + newSpan * (1 - frac));
    m_pausedSpanMs = newSpan;
    update();
    e->accept();
}

void RealtimeChart::leaveEvent(QEvent *)
{
    m_cursorX = -1;
    update();
}
