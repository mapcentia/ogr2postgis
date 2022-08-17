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
    argparse::ArgumentParser program("ogr2postgis", "2022.5.0");
    program.add_argument("-o", "--schema").help("Output PostgreSQL schema, Defaults to public.");
    program.add_argument("-t", "--t_srs").help(
            "Fallback target SRS. Will be used if no authority name/code is available. Defaults to EPSG:4326.");
    program.add_argument("-s", "--s_srs").help(
            "Fallback source SRS. Will be used if file doesn't contain projection information.");
    program.add_argument("-n", "--nln").help(
            "Alternative table name. Can only be used when importing single file - not directories unless --append is used.");
    program.add_argument("-e", "--encoding").help("Fallback encoding. Will be used if UTF8 fails").default_value(
            std::string{"LATIN1"});
    program.add_argument("-i", "--import").help("Import found files into PostgreSQL/PostGIS").default_value(
            false).implicit_value(true);
    program.add_argument("-p", "--p_multi").help("Promote single geometries to multi part.").default_value(
            false).implicit_value(true);
    program.add_argument("-a", "--append").help("Append to existing layer instead of creating new.").default_value(
            false).implicit_value(true);
    program.add_argument("-c", "--connection").help(
            "PGDATASOURCE postgres datasource. E.g.\"PG:host='addr' dbname='databasename' port='5432' user='x' password='y'\"");
    program.add_argument("path").help("[DIRECTORY|FILE]");
//    program.add_epilog("Possible things include betingalw, chiz, and res.");


//    Config config = Config();

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
//        config.connection = program.get("c");
    }
    if (program.present("--schema")) {
        schema = program.get("o");
//        config.schema = program.get("o");
    } else {
        schema = "public";
//        config.schema = "public";
    }
    if (program["--import"] == true) {
        import = true;
//        config.import = true;
    };
    if (program.present("--t_srs")) {
        t_srs = program.get("t");
//        config.t_srs = program.get("t");
    }
    if (program.present("--s_srs")) {
        s_srs = program.get("s");
//        config.s_srs = program.get("s");
    }
    if (program.present("--nln")) {
        nln = program.get("n");
//        config.nln = program.get("n");
    }

    fallbackEncoding = program.get<std::string>("--encoding");

    if (program["--append"] == true) {
        append = true;
//        config.append = true;
    };
    if (program["--p_multi"] == true) {
        p_multi = true;
//        config.p_multi = true;

    };

    auto path = program.get("path");
    start(path);
}



