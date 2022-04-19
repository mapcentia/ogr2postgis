/*
 * @author     Martin Høgh <mh@mapcentia.com>
 * @copyright  2013-2022 MapCentia ApS
 * @license    http://www.gnu.org/licenses/#AGPL  GNU AFFERO GENERAL PUBLIC LICENSE 3
 */

#include "ogr2postgis.hpp"
#include "argparse.hpp"

using namespace std;
using namespace ogr2postgis;

int main(int argc, char *argv[]) {
    argparse::ArgumentParser program("ogr2postgis");
    program.add_argument("-c", "--connection").help("Hej");
    program.add_argument("-o", "--schema");
    program.add_argument("-t", "--t_srs");
    program.add_argument("-s", "--s_srs");
    program.add_argument("-n", "--nln");
    program.add_argument("-i", "--import").help("Hej").default_value(false).implicit_value(true);
    program.add_argument("-p", "--p_multi").default_value(false).implicit_value(true);
    program.add_argument("-a", "--append").default_value(false).implicit_value(true);
    program.add_argument("path")
            .help("display the square of a given number");

    try {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error &err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }
    if (program.present("--connection")) {
        connection = program.get("c");
    }
    if (program.present("--schema")) {
        schema = program.get("o");
    } else {
        schema = "public";
    }
    if (program["--import"] == true) {
        import = true;
    };
    if (program.present("--t_srs")) {
        t_srs = program.get("t");
    }
    if (program.present("--s_srs")) {
        s_srs = program.get("s");
    }
    if (program.present("--nln")) {
        nln = program.get("n");
    }
    if (program["--append"] == true) {
        append = true;
    };
    if (program["--p_multi"] == true) {
        p_multi = true;
    };

    auto path = program.get("path");
    start(path);
}



