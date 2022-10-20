release = ["-DCMAKE_BUILD_TYPE=Release"]
debug = ["-DCMAKE_BUILD_TYPE=Debug"]
release_no_lp = ["-DCMAKE_BUILD_TYPE=Release", "-DUSE_LP=NO"]
# USE_GLIBCXX_DEBUG is not compatible with USE_LP (see issue983).
glibcxx_debug = ["-DCMAKE_BUILD_TYPE=Debug", "-DUSE_LP=NO", "-DUSE_GLIBCXX_DEBUG=YES"]
minimal = ["-DCMAKE_BUILD_TYPE=Release", "-DDISABLE_PLUGINS_BY_DEFAULT=YES"]

_plugins = ["PLUGIN_ASTAR", "COST_SATURATION", "OPERATOR_COUNTING"]
_project = ["-DDISABLE_PLUGINS_BY_DEFAULT=YES"] + [f"-DPLUGIN_{plugin}_ENABLED=YES" for plugin in _plugins]
project = ["-DCMAKE_BUILD_TYPE=Release"] + _project
projectdebug = ["-DCMAKE_BUILD_TYPE=Debug"] + _project
del _plugins
del _project

DEFAULT = "release"
DEBUG = "debug"
