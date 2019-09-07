#include <thread>
#include <chrono>
#include <random>

#include "countly.hpp"

int main() {
	Countly& countly = Countly::getInstance();
	countly.setMetrics("Windows", "10.22", "1920x1080", "Samsung Chromebook", "AT&T", "1.2.0");
	countly.start("a32cb06789a6e99958d628378ee66bf8583a454f", // Your Countly application key
		      "41aa9adf-2f2a-44fa-93de-b705e577c01f",     // Device ID, can be any string as long as its format is consistent across your Countly application
		      "https://your.countly.instance",            // URL of your Countly API server
		      443,                                        // TCP port of your Countly API server
		      true);                                      // Start the update loop on a new thread

	std::default_random_engine rng;
	for (int i = 0; i < 100; i++) {
		// Create a "click" event with the segmentation of random coordinates on a 1080p screen.
		Countly::Event event("click", 1);
		event.addSegmentation("x", rng() % 1920);
		event.addSegmentation("y", rng() % 1080);

		// Add the event to the event queue.
		countly.addEvent(event);

		/* Since the update loop is running on another thread, we just need to wait a bit.
		 * Normally, your application would do actual work in this period of time. */
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}

	/* This isn't really needed as the Countly singleton will be destroyed when the program exits
	 * and its deconstructor calls stop() already. */
	countly.stop();
}
