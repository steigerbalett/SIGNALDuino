try:
    import configparser
except ImportError:
    import ConfigParser as configparser

Import("env")

### access to global build environment
#print(env)

### view all build environment
#print(env.Dump())

### to build date str and buildname
import datetime
import subprocess
import os
import platform
date = datetime.datetime.now().strftime("%y%m%d")

# for config.get
# config = configparser.ConfigParser()
# config.read("platformio.ini")
# build_version = config.get("env", "BUILD_FLAGS")

# name from env:project | example: [env:nano_bootl_old_CC1101] 
build_name = "SIGNALDuino_" + env['PIOENV']

# Build versions with a point are difficult to process
# System uses dot for file extension

import re

def git_basetag():
    # latest tag, forks / shallow clones may have no tags -> version from compile_config.h + commit
    try:
        return (
            subprocess.check_output(["git", "describe", "--tags", "--first-parent", "--abbrev=1"], stderr=subprocess.DEVNULL)
            .strip()
            .decode("utf-8")
        )
    except Exception:
        pass
    version = "0.0.0"
    try:
        with open(os.path.join("src", "compile_config.h"), encoding="utf-8") as f:
            m = re.search(r'#define\s+PROGVERS\s+"([0-9.]+)', f.read())
            if m:
                version = m.group(1)
    except Exception:
        pass
    try:
        sha = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], stderr=subprocess.DEVNULL).strip().decode("utf-8")
        return version + "-g" + sha
    except Exception:
        return version

reftype = os.environ.get('GITHUB_REF_TYPE','local')
basetag = git_basetag()
# GITHUB_REF_SLUG is set by rlespinasse/github-slug-action, else use GITHUB_REF_NAME
refslug = os.environ.get('GITHUB_REF_SLUG') or re.sub(r'[^a-zA-Z0-9._-]', '-', os.environ.get('GITHUB_REF_NAME', ''))

if (reftype == 'branch') :
    build_version = basetag+refslug+"+"+date
elif (reftype == 'tag') :
    build_version = refslug or (basetag+"+"+date)
else:
    build_version = basetag+"+"+date

# write project hex, bin, elf to nano_bootl_old_CC1101_v350_dev_20200811.hex
env.Replace(PROGNAME="%s" % build_name + "_" + "%s" % build_version)

env.Append(CPPDEFINES=[
    ("PROGVERS",env.StringifyMacro(build_version)),
])


#Copy functions has to be finished, it does currently not work
def copy_firmware(source, target, env):

    from pathlib import Path
    import shutil
    filename= source
    if env["PIOPLATFORM"] == "espressif32" or env["PIOPLATFORM"] == "espressif8266":
        filename = source + ".bin"
    else:
        filename = source + ".hex"
    
    print (filename)
    ## if bin file exists, copy to subfolder
    # shutil.copyfile(str(filename), str(BUILD_DIR))


#if platform.system() == "Windows":
#    env.AddPostAction("$BUILD_DIR\\${PROGNAME}", copy_firmware)

#if platform.system() == "Linux":
#    env.AddPostAction("$BUILD_DIR/${PROGNAME}", copy_firmware)
