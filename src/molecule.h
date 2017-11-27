#pragma once

#include "core.h"
#include "io.h"
#include "geometry.h"

namespace Faunus {

    /**
    * @brief Random position and orientation - typical for rigid bodies
    *
    * Molecule inserters take care of generating molecules
    * for insertion into space and can be used in Grand Canonical moves,
    * Widom analysis, and for generating initial configurations.
    * Inserters will not actually insert anything, but rather
    * return a particle vector with proposed coordinates.
    *
    * All inserters are function objects, expecting
    * a geometry, particle vector, and molecule data.
    */
    template<typename TMoleculeData>
    struct RandomInserter
    {
        typedef typename TMoleculeData::TParticleVector Tpvec;
        std::string name;
        Point dir;         //!< Scalars for random mass center position. Default (1,1,1)
        Point offset;      //!< Added to random position. Default (0,0,0)
        bool checkOverlap; //!< Set to true to enable container overlap check
        bool rotate;       //!< Set to true to randomly rotate molecule when inserted. Default: true
        bool keeppos;      //!< Set to true to keep original positions (default: false)
        int maxtrials;     //!< Maximum number of overlap checks if `checkOverlap==true`

        RandomInserter() : dir(1, 1, 1), offset(0, 0, 0), checkOverlap(true), keeppos(false), maxtrials(2e3)
        {
            name = "random";
        }

        Tpvec operator()( Geometry::GeometryBase &geo, const Tpvec &p, TMoleculeData &mol )
        {
            Random slump;
            bool _overlap = true;
            Tpvec v;
            int cnt = 0;
            do
            {
                if ( cnt++ > maxtrials )
                throw std::runtime_error("Max. # of overlap checks reached upon insertion.");

                v = mol.getRandomConformation();

                if ( mol.isAtomic())
                { // insert atomic species
                    for ( auto &i : v )
                    { // for each atom type id
                        QuaternionRotate rot;
                        if ( rotate )
                        {
                            rot.set(2*pc::pi*slump(), ranunit(slump));
                            i.rotate(rot);
                        }
                        geo.randompos(i, slump);
                        i = i.cwiseProduct(dir) + offset;
                        geo.boundary(i);
                    }
                }
                else
                { // insert molecule
                    if ( keeppos )
                    {                   // keep original positions (no rotation/trans)
                        for ( auto &i : v )              // ...but let's make sure it fits
                        if ( geo.collision(i, 0))
                        throw std::runtime_error("Error: Inserted molecule does not fit in container");
                    }
                    else
                    {                         // generate a now position/orientation
                        Point a;
                        geo.randompos(a, slump);       // random point in container
                        a = a.cwiseProduct(dir);       // apply user defined directions (default: 1,1,1)
                        Geometry::cm2origo(geo, v);    // translate to origo - obey boundary conditions
                        QuaternionRotate rot;
                        rot.set(slump()*2*pc::pi, ranunit(slump)); // random rot around random vector
                        for ( auto &i : v )
                        {            // apply rotation to all points
                            if ( rotate )
                            i = rot(i) + a + offset;   // ...and translate
                            else
                            i += a + offset;
                            geo.boundary(i);             // ...and obey boundaries
                        }
                    }
                }

                assert(!v.empty());

                _overlap = false;
                if ( checkOverlap )              // check for container overlap
                for ( auto &i : v )
                if ( geo.collision(i, i.radius))
                {
                    _overlap = true;
                    break;
                }
            }
            while ( _overlap == true );
            return v;
        }
    };

    /**
     * @brief General properties for molecules
     */
    template<class Tpvec>
        class MoleculeData {
            private:
                int _id=-1;
                int _confid;
                Random slump;
            public:
                /** @brief Signature for inserted function */
                typedef std::function<Tpvec( Geometry::GeometryBase &, const Tpvec &, MoleculeData<Tpvec> & )> TinserterFunc;
                TinserterFunc inserterFunctor;              //!< Function for insertion into space

                int& id() { return _id; } //!< Type id
                const int& id() const { return _id; } //!< Type id

                int Ninit = 0;             //!< Number of initial molecules
                std::string name;          //!< Molecule name
                std::string structure;     //!< Structure file (pqr|aam|xyz)
                bool atomic=false;         //!< True if atomic group (salt etc.)
                bool rotate=true;          //!< True if molecule should be rotated upon insertion
                bool keeppos=false;        //!< Keep original positions of `structure`
                double activity=0;         //!< Chemical activity (mol/l)
                Point insdir = {1,1,1};    //!< Insertion directions
                Point insoffset = {0,0,0}; //!< Insertion offset

                std::vector<int> atoms;    //!< Sequence of atoms in molecule (atom id's)
                std::vector<Tpvec> conformations;           //!< Conformations of molecule
                std::discrete_distribution<> confDist;      //!< Weight of conformations

                void addConformation( const Tpvec &vec, double weight = 1 )
                {
                    if ( !conformations.empty())
                    {     // resize weights
                        auto w = confDist.probabilities();// (defaults to 1)
                        w.push_back(weight);
                        confDist = std::discrete_distribution<>(w.begin(), w.end());
                    }
                    conformations.push_back(vec);
                    assert(confDist.probabilities().size() == conformations.size());
                } //!< Store a single conformation

                /** @brief Specify function to be used when inserting into space.
                *
                * By default a random position and orientation is generator and overlap
                * with container is avoided.
                */
                void setInserter( const TinserterFunc &ifunc ) { inserterFunctor = ifunc; };

                /**
                * @brief Get a random conformation
                *
                * This will return the raw coordinates of a random conformation
                * as loaded from a directory file. The propability of a certain
                * conformation is dictated by the weight which, by default,
                * is set to unity. Specify a custom distribution using the
                * `weight` keyword.
                */
                Tpvec getRandomConformation()
                {
                    if ( conformations.empty())
                    throw std::runtime_error("No configurations for molecule '" + name +
                    "'. Perhaps you forgot to specity the 'atomic' keyword?");

                    assert(size_t(confDist.max()) == conformations.size() - 1);
                    assert(atoms.size() == conformations.front().size());

                    _confid = confDist(slump.engine); // store the index of the conformation
                    return conformations.at( _confid );
                }

                /**
                * @brief Get random conformation that fits in container
                * @param geo Geometry
                * @param otherparticles Typically `spc.p` is insertion depends on other particle
                *
                * By default the molecule is placed at a random position and orientation with
                * no container overlap using the `RandomInserter` class. This behavior can
                * be changed by specifying another inserter using `setInserter()`.
                */
                Tpvec getRandomConformation(Geometry::GeometryBase &geo, Tpvec otherparticles = Tpvec())
                {
                    return inserterFunctor(geo, otherparticles, *this);
                }

                /**
                * @brief Store a single conformation
                * @param vec Vector of particles
                * @param weight Relative weight of conformation (default: 1)
                */
                void pushConformation( const Tpvec &vec, double weight = 1 )
                {
                    if ( !conformations.empty())
                    {     // resize weights
                        auto w = confDist.probabilities();// (defaults to 1)
                        w.push_back(weight);
                        confDist = std::discrete_distribution<>(w.begin(), w.end());
                    }
                    conformations.push_back(vec);
                    assert(confDist.probabilities().size() == conformations.size());
                }

                /** @brief Nunber of conformations stored for molecule */
                size_t numConformations() const
                {
                    return conformations.size();
                }

                void loadConformation(const std::string &file)
                {
                    Tpvec v;
                    if (loadStructure<Tpvec>()(file, v, false))
                    {
                        if ( keeppos == false )
                            Geometry::cm2origo( v.begin(), v.end() ); // move to origo
                        pushConformation(v);        // add conformation
                        for ( auto &p : v )           // add atoms to atomlist
                            atoms.push_back(p.id);
                    }
                    if ( v.empty() )
                        throw std::runtime_error("Structure " + structure + " not loaded. Filetype must be .aam/.pqr/.xyz");
                }
        }; // end of class

    template<class Tpvec>
        void to_json(json& j, const MoleculeData<Tpvec> &a) {
            auto& _j = j[a.name];
            _j["activity"] = a.activity / 1.0_molar;
            _j["atomic"] = a.atomic;
            _j["id"] = a.id();
            _j["insdir"] = a.insdir;
            _j["insoffset"] = a.insoffset;
            _j["keeppos"] = a.keeppos;
        }

    template<class Tpvec>
        void from_json(const json& j, MoleculeData<Tpvec> & a) {
            if (j.is_object()==false || j.size()!=1)
                throw std::runtime_error("Invalid JSON data for AtomData");
            for (auto it=j.begin(); it!=j.end(); ++it) {
                a.name = it.key();
                auto& val = it.value();
                a.activity = val.value("activity", a.activity) * 1.0_molar;
                a.atomic = val.value("atomic", a.atomic);
                a.id() = val.value("id", a.id());
                a.insdir = val.value("insdir", a.insdir);
                a.insoffset = val.value("insoffset", a.insoffset);
                a.keeppos = val.value("keeppos", a.keeppos);
                a.structure = val.value("structure", a.structure);
                if (!a.structure.empty())
                    a.loadConformation(a.structure);
            }
        }

    template<typename Tpvec>
        static std::vector<MoleculeData<Tpvec>> molecules = {}; //!< Global instance of molecule list

#ifdef DOCTEST_LIBRARY_INCLUDED
    TEST_CASE("[Faunus] MoleculeData") {
        using doctest::Approx;

        json j = {
            { "moleculelist",
                {
                    { "B",
                        {
                            {"activity",0.2}, {"atomic",true},
                            {"insdir",{0.5,0,0}}, {"insoffset",{-1.1, 0.5, 10}}
                        }
                    },
                    { "A", { {"atomic",false} } }
                }
            }
        };
        typedef Particle<Radius, Charge, Dipole, Cigar> T;
        typedef std::vector<T> Tpvec;
        typedef MoleculeData<Tpvec> Tmoldata;

        molecules<Tpvec> = j["moleculelist"].get<decltype(molecules<Tpvec>)>(); // fill global instance
        auto &v = molecules<Tpvec>; // reference to global molecule vector

        CHECK(v.size()==2);
        CHECK(v.front().id()==0);
        CHECK(v.front().name=="A"); // alphabetic order in std::map
        CHECK(v.front().atomic==false);

        MoleculeData<Tpvec> m = json(v.back()); // moldata --> json --> moldata

        CHECK(m.name=="B");
        CHECK(m.id()==1);
        CHECK(m.activity==Approx(0.2_molar));
        CHECK(m.atomic==true);
        CHECK(m.insdir==Point(0.5,0,0));
        CHECK(m.insoffset==Point(-1.1,0.5,10));
    }
#endif

}//namespace