
class ThingerMonitorRestore {

protected:
    ThingerMonitorConfig config_;
    std::string name_;
    std::string tag_;

    ThingerMonitorRestore(ThingerMonitorConfig& config, const std::string& name, const std::string& tag)
      : config_(config), name_(name), tag_(tag)
    {}

public:

    virtual int download() = 0;
    virtual void restore() = 0;
    virtual void clean() = 0;

    virtual ~ThingerMonitorRestore() = default;

};
