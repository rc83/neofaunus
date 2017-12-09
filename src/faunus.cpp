#include "core.h"
#include "move.h"
#include "multipole.h"
#include "docopt.h"
#include <cstdlib>

using namespace Faunus;
using namespace std;

typedef Geometry::Cuboid Tgeometry;
typedef Particle<Radius, Charge> Tparticle;

static const char USAGE[] =
R"(Faunus - A Framework for Molecular Simulation.

    http://github.com/mlund/faunus

    Usage:
      faunus [-q] [--rerun=TRAJ] [--state=FILE] [-]
      faunus (-h | --help)
      faunus --version

    Options:
      --rerun=TRAJ               Rerun w. trajectory file (.xtc).
      -s FILE, --state=FILE      Initialize using state file.
      -q, --quiet                Less verbose output.
      -h --help                  Show this screen.
      --version                  Show version.
)";

int main( int argc, char **argv )
{
    try {
        auto args = docopt::docopt( USAGE,
                { argv + 1, argv + argc }, true, "Faunus 2.0.0");

        if ( args["--quiet"].asBool() )
            cout.setstate( std::ios_base::failbit ); // hold kæft!

        json j;
        std::cin >> j;

        MCSimulation<Tgeometry,Tparticle> sim(j);
        Analysis::CombinedAnalysis analysis(j, sim.space(), sim.pot());

        auto& loop = j.at("mcloop");
        int macro = loop.at("macro");
        int micro = loop.at("micro");

        for (int i=0; i<macro; i++) {
            for (int j=0; j<micro; j++) {
                sim.move();
                analysis.sample();
            }
            cout << "relative drift = " << sim.drift() << endl;
        }

        std::ofstream f("out.json");
        if (f) {
            json j = sim;
            j["analysis"] = analysis;
            f << std::setw(4) << j << endl;
        }

    } catch (std::exception &e) {
        std::cerr << e.what() << endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}