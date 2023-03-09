import os
import datetime
import logging
import itertools

# Set up logging
logging.basicConfig(filename='test_results.log', level=logging.INFO, format='%(asctime)s:%(levelname)s:%(message)s')
# List all combinations of COUNTLY_USE_CUSTOM_SHA256 and COUNTLY_USE_SQLITE
options = [(0, 0), (0, 1), (1, 0), (1, 1)]

for custom_sha256, use_sqlite in options:
    print("Running script with COUNTLY_USE_CUSTOM_SHA256="+str(custom_sha256)+" and COUNTLY_USE_SQLITE="+str(use_sqlite))
    logging.info("Running script with COUNTLY_USE_CUSTOM_SHA256="+str(custom_sha256)+" and COUNTLY_USE_SQLITE="+str(use_sqlite))


    # Check if script is in the "build" folder
    if os.path.basename(os.getcwd()) == "build":
        print("In build folder. Changing directory...")
        logging.info("In build folder. Changing directory...")
        os.chdir("..")
        print("Current directory:", os.getcwd())
        logging.info("Current directory: {}".format(os.getcwd()))
    else:
        print("Not in build folder. Current directory:", os.getcwd())
        logging.info("Not in build folder. Current directory: {}".format(os.getcwd()))

    # Delete "build" folder and subfolders
    print("Deleting folder and its subfolders...")
    logging.info("Deleting folder and its subfolders...")
    os.system("rm -rf build")
    print("Folder and subfolders deleted.")
    logging.info("Folder and subfolders deleted.")


    # Run cmake to generate makefiles
    build_dir = "build"
    cmake_command = "cmake -DCOUNTLY_BUILD_SAMPLE=1 -DCOUNTLY_BUILD_TESTS=1 -DCOUNTLY_USE_CUSTOM_SHA256={} -DCOUNTLY_USE_SQLITE={} -B {} .".format(custom_sha256, use_sqlite, build_dir)
    os.system(cmake_command)
    logging.info("Ran cmake to generate makefiles.")

    # Change directory to "build" and build the sample and tests
    os.chdir("build")
    print("Current directory:", os.getcwd())
    logging.info("Current directory: {}".format(os.getcwd()))
    os.system("make ./countly-sample")
    os.system("make ./countly-tests")

    # Redirect standard output and standard error to a file
    output_file = "doctest_results.txt"
    with open(output_file, "w") as f:
        os.system("./countly-tests > {} 2>&1".format(output_file))

    # Include doctest results in the log file
    with open(output_file, "r") as f:
        doctest_results = f.read()
    logging.info("Doctest results:\n{}".format(doctest_results))

    # Clean up
    os.remove(output_file)
    logging.info("Removed doctest output file.")

    # Print status message
    print("Done.")
    logging.info("Script finished.")