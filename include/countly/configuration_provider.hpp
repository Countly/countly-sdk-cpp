#ifndef CONFIGURATION_PROVIDER_HPP_
#define CONFIGURATION_PROVIDER_HPP_
namespace cly {

struct SDKLimits {
  unsigned int maxKeyLength;
  unsigned int maxValueSize;
  unsigned int maxSegmentationValues;
  unsigned int maxBreadcrumbCount;
  unsigned int maxStackTraceLinesPerThread;
  unsigned int maxStackTraceLineLength;
};

class ConfigurationProvider {
public:
    virtual ~ConfigurationProvider() = default;

    virtual bool isNetworkingEnabled() const = 0;
    virtual bool isTrackingEnabled() const = 0;
    virtual bool isCrashReportingEnabled() const = 0;
    virtual bool isViewTrackingEnabled() const = 0;
    virtual unsigned int getRequestQueueSizeLimit() const = 0;
    virtual SDKLimits getLimits() const = 0;
};
} // namespace cly
#endif