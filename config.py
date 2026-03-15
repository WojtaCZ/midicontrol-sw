import configparser
import os

def load():
    # Create a configuration object
    config = configparser.ConfigParser()

    # Read the available config file
    if config.read("config.conf"):
        print("Reading config file config.conf.")
    else:
        print("Unable to find the config file!")
        exit(1)

    # Check if a config file is empty
    if not config.sections():
        print("Config file is empty!")
        exit(1)

    # Check if file has any config present in the correct section
    if not config["FILES"]:
        print("Config file doesn't contain any config!")
        exit(1)

    # Try to load the play folder config
    try:
        global playFolder
        playFolder = config["FILES"]["PlayFolder"]
    except KeyError:
        print("PlayFolder is not specified in config file!")
        exit(1)

    # Try to load the record folder config
    try:
        global recFolder
        recFolder = config["FILES"]["RecordFolder"]
    except KeyError:
        print("RecordFolder is not specified in config file!")
        exit(1)

    #Check if the specified play folder is actually present on the system
    if os.path.isdir(playFolder):
        print("Successfully found the play folder.")
    else:
        print("Unable to find the play folder!")
        exit(1)

    #Check if the specified record folder is actually present on the system
    if os.path.isdir(recFolder):
        print("Successfully found the recordings folder.")
    else:
        print("Unable to find the recordings folder!")
        exit(1)

    # Check if file has any config present in the bind section
    if config["BINDINGS"]:
        print("Found configuration in the bind section.")
        global bindings
        bindings = config["BINDINGS"]

def getRecFolder():
    return recFolder

def getPlayFolder():
    return playFolder

def getBinding(name):
    return bindings[name]
