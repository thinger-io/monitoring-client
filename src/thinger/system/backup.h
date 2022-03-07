
class ThingerMonitorBackup {

protected:
    ThingerMonitorConfig config_;
    std::string name_;

    ThingerMonitorBackup(ThingerMonitorConfig& config, const std::string& name) : config_(config), name_(name) {}

public:

    virtual void create() = 0;
    virtual int upload() = 0;
    virtual void clean() = 0;

    virtual ~ThingerMonitorBackup() = default;

};
