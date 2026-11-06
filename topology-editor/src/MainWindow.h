#pragma once

#include <QMainWindow>

class TopologyScene;
class TopologyView;
class MiniMapView;
class PropertyModel;
class AlarmLogModel;
class AlarmFilterProxy;
class AlarmSimulator;
class CommManager;
class DeviceMonitor;
class DevicePanel;
class HistoryManager;
class DashboardBackend;
class QQuickView;
enum class LinkState;
class QUndoStack;
class QLabel;
class QActionGroup;
class QComboBox;
class QAction;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void createDocks();
    void createStatusBar();

    void newTopology();
    void openTopology();
    void saveTopology();
    void loadBytes(const QByteArray &data, const QString &source);
    void loadSample();
    void runStressTest(int count);
    void resetForRebuild();

    void deleteSelection();
    void alignSelection(Qt::Alignment how);
    void onSelectionChanged();
    void locateNode(int id);
    void updateStats();
    void showAbout();
    void showHelp();
    void startDemo();
    void onLinkStateChanged(LinkState state);
    void afterRebuild();
    QList<int> virtualDeviceIds() const;

    // 监控大屏：独立的 Qt Quick 窗口（桌面版可以拖到第二块屏幕全屏），关闭即销毁
    void showDashboard();
    void closeDashboard();
    void toggleDashboardFullScreen();

    QUndoStack *m_undo;
    TopologyScene *m_scene;
    TopologyView *m_view;
    MiniMapView *m_miniMap = nullptr;
    PropertyModel *m_props = nullptr;
    AlarmLogModel *m_alarmLog = nullptr;
    AlarmFilterProxy *m_alarmProxy = nullptr;
    AlarmSimulator *m_sim;
    CommManager *m_comm = nullptr;
    DeviceMonitor *m_monitor = nullptr;
    DevicePanel *m_devicePanel = nullptr;
    HistoryManager *m_history = nullptr;
    DashboardBackend *m_backend = nullptr;
    QQuickView *m_dashboard = nullptr;
    QAction *m_dashAct = nullptr;

    QActionGroup *m_modeGroup = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QAction *m_simAction = nullptr;
    QMenu *m_viewMenu = nullptr;

    QLabel *m_statCount = nullptr;
    QLabel *m_statStatus = nullptr;
    QLabel *m_statZoom = nullptr;
    QLabel *m_statFrame = nullptr;
    QLabel *m_statLink = nullptr;
};
