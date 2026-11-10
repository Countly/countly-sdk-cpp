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

    /** Hours after which a stored request is dropped without being sent, 0 to keep requests however old they are. */
    virtual unsigned int getRequestDropAgeHours() const { return 0; }

    /** Whether the backoff mechanism is on. */
    virtual bool isBackoffEnabled() const { return true; }

    /** Seconds of response time from which an answer counts as slow. */
    virtual unsigned int getBackoffAcceptedTimeout() const { return 10; }

    /** Share of the request queue size, above 0 and at most 1, up to which a slow answer starts a backoff. */
    virtual double getBackoffRequestQueuePercentage() const { return 0.5; }

    /** Hours of request age up to which a slow answer starts a backoff. */
    virtual unsigned int getBackoffRequestAge() const { return 24; }

    /** Seconds a backoff pauses sending. */
    virtual unsigned int getBackoffDuration() const { return 60; }
};
} // namespace cly
#endif