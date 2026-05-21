#include "translate_options.h"

namespace translate {
Options &get_options() {
    static Options options;
    return options;
}
}
