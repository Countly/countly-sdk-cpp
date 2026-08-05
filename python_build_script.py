#!/usr/bin/env python3
import os
import sys
import shutil
import subprocess
import datetime
import logging

# Setup
log_filename = 'test_results_{}.log'.format(datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
logging.basicConfig(filename=log_filename, level=logging.INFO, format='%(asctime)s:%(levelname)s:%(message)s')
console = logging.StreamHandler()
console.setLevel(logging.INFO)
logging.getLogger().addHandler(console)

is_windows = os.name == 'nt'
repo_root = os.path.abspath(os.getcwd())

options = [(0, 0), (0, 1), (1, 0), (1, 1)]
build_dir = os.path.join(repo_root, 'build')
config = 'Debug'  # change to Release if needed

for custom_sha256, use_sqlite in options:
    cfg_name = f'custom_sha256={custom_sha256}, sqlite={use_sqlite}'
    print(f"\n=== Running configuration: {cfg_name} ===")
    logging.info("Running configuration: %s", cfg_name)

    try:
        # Ensure we're at repo root
        if os.path.basename(os.getcwd()) == 'build':
            os.chdir('..')

        # Remove build directory
        if os.path.isdir(build_dir):
            logging.info("Removing existing build directory: %s", build_dir)
            shutil.rmtree(build_dir)
        os.makedirs(build_dir, exist_ok=True)

        # Configure with CMake
        cmake_args = [
            'cmake',
            '-DCOUNTLY_BUILD_SAMPLE=1',
            '-DCOUNTLY_BUILD_TESTS=1',
            f'-DCOUNTLY_USE_CUSTOM_SHA256={custom_sha256}',
            f'-DCOUNTLY_USE_SQLITE={use_sqlite}',
            '-B', build_dir,
            repo_root
        ]
        logging.info("Running cmake configure: %s", ' '.join(cmake_args))
        subprocess.run(cmake_args, check=True)

        # Build sample and tests via cmake --build (cross platform)
        build_sample_cmd = ['cmake', '--build', build_dir, '--config', config, '--target', 'countly-sample']
        build_tests_cmd = ['cmake', '--build', build_dir, '--config', config, '--target', 'countly-tests']
        logging.info("Building sample: %s", ' '.join(build_sample_cmd))
        subprocess.run(build_sample_cmd, check=True)

        logging.info("Building tests: %s", ' '.join(build_tests_cmd))
        subprocess.run(build_tests_cmd, check=False)  # tests might be absent depending on CMake options

        # Run tests: prefer ctest, fallback to running test executable
        output_file = os.path.join(build_dir, f'doctest_results_{custom_sha256}_{use_sqlite}.txt')
        try:
            # Try ctest first
            ctest_cmd = ['ctest', '--test-dir', build_dir, '-C', config, '--output-on-failure']
            logging.info("Running ctest: %s", ' '.join(ctest_cmd))
            with open(output_file, 'w', encoding='utf-8') as out:
                completed = subprocess.run(ctest_cmd, stdout=out, stderr=subprocess.STDOUT, check=False)
            logging.info("ctest exit code: %s", completed.returncode)
        except FileNotFoundError:
            # ctest not available: try to run the test binary directly
            logging.info("ctest not available; attempting to run test binary directly")
            test_bin_name = 'countly-tests.exe' if is_windows else 'countly-tests'
            test_path = os.path.join(build_dir, config, test_bin_name) if is_windows else os.path.join(build_dir, test_bin_name)
            if os.path.exists(test_path):
                with open(output_file, 'w', encoding='utf-8') as out:
                    subprocess.run([test_path], stdout=out, stderr=subprocess.STDOUT, check=False)
            else:
                logging.warning("Test binary not found at %s", test_path)

        # Read and log results
        if os.path.exists(output_file):
            with open(output_file, 'r', encoding='utf-8') as f:
                doctest_results = f.read()
            logging.info("Test output for %s:\n%s", cfg_name, doctest_results)
        else:
            logging.warning("No test output file produced for %s", cfg_name)

    except subprocess.CalledProcessError as e:
        logging.error("Build or command failed for %s: %s", cfg_name, str(e))
    except Exception as e:
        logging.exception("Unexpected error in configuration %s: %s", cfg_name, str(e))
    finally:
        # Optionally keep outputs or clean up
        logging.info("Completed configuration: %s", cfg_name)

print("All configurations finished.")