import os
import subprocess
import shutil
Import("env")

import sys
from SCons.Script import ARGUMENTS, COMMAND_LINE_TARGETS

# Print raw arguments for debugging
# print("sys.argv:", sys.argv)
# print("COMMAND_LINE_TARGETS:", COMMAND_LINE_TARGETS)

def before_build_fs():
    print("Compiling Tailwind CSS...")

    error = 0


    # Step 1: Compile Tailwind CSS
    subprocess.run("build_css.bat", check=True)
    print("Tailwind CSS compiled successfully!")


    # Step 2: Define source and destination directories
    source_dir = "./src/www"
    dest_dir = "./data"

    # Step 3: List of files or patterns to ignore
    ignored_files = [
        "input.css"
    ]

    # Step 4: Walk through source directory and copy files while excluding ignored ones
    # if not os.path.exists(dest_dir):
    #     os.makedirs(dest_dir)

    source_dir = os.path.join(os.getcwd(),source_dir)

    for root, dirs, files in os.walk(source_dir):
        # Determine relative path from source directory
        rel_path = os.path.relpath(root, source_dir)
        dest_path = os.path.join(dest_dir, rel_path)

        # Ensure destination directory exists
        if not os.path.exists(dest_path):
            os.makedirs(dest_path)

        for file in files:
            if file in ignored_files:
                continue
            source_file = os.path.join(root, file)
            print(source_file)
            dest_file = os.path.join(dest_path, file)

            # Copy file to destination
            shutil.copy2(source_file, dest_file)

    print("Successfully built www files")

if "buildfs" in COMMAND_LINE_TARGETS:
    print("Filesystem build requested.")
    before_build_fs()
elif "uploadfs" in COMMAND_LINE_TARGETS:
    print("Filesystem upload requested.")   
    before_build_fs()
else:
    print("No filesystem upload/build requested.")