release = ["-DCMAKE_BUILD_TYPE=Release"]
debug = ["-DCMAKE_BUILD_TYPE=Debug"]
releasenolp = ["-DCMAKE_BUILD_TYPE=Release", "-DUSE_LP=NO"]
debugnolp = ["-DCMAKE_BUILD_TYPE=Debug", "-DUSE_LP=NO"]
minimal = ["-DCMAKE_BUILD_TYPE=Release", "-DDISABLE_PLUGINS_BY_DEFAULT=YES"]

_plugins = ["PLUGIN_ASTAR", "COST_SATURATION", "OPERATOR_COUNTING"]
_project = ["-DDISABLE_PLUGINS_BY_DEFAULT=YES"] + [f"-DPLUGIN_{plugin}_ENABLED=YES" for plugin in _plugins]
project = ["-DCMAKE_BUILD_TYPE=Release"] + _project
#projectdebug = ["-DCMAKE_BUILD_TYPE=Debug"] + _project

DEFAULT = "release"
DEBUG = "debug"
