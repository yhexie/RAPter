#ifndef GF2_MERGING_HPP
#define GF2_MERGING_HPP

#include "globfit2/optimization/merging.h"

#include "globfit2/parameters.h"
#include "globfit2/visualization/visualization.h"
#include "globfit2/io/io.h"
#include "globfit2/processing/util.hpp"          //getPopulations()
#include "globfit2/optimization/patchDistanceFunctors.h" // RepresentativeSqrPatchPatchDistanceFunctorT
#include "globfit2/util/util.hpp"

#define CHECK(err,text) { if ( err != EXIT_SUCCESS )  std::cerr << "[" << __func__ << "]: " << text << " returned an error! Code: " << err << std::endl; }

namespace GF2 {

namespace merging
{
    /*! \brief Dummy concept, how to use \ref processing::transformPrimitiveMap in globfit2/processing/util.hpp.
     */
    template <class _PrimitiveT>
    struct RefitFunctor
    {
            int eval( _PrimitiveT& prim ) const
            {
                std::cout << "[" << __func__ << "]: "
                          << "transforming primitive with GID: " << prim.getTag( _PrimitiveT::GID )
                          << ", and DIR_GID: " << prim.getTag( _PrimitiveT::DIR_GID )
                          << std::endl;
                return 0;
            } //...eval()
    };
}

template < class    _PrimitiveContainerT
         , class    _PointContainerT
         , typename _Scalar
         , class    _PointPrimitiveT
         , class    _PrimitiveT
         >
int
Merging::mergeCli( int argc, char** argv )
{
    MergeParams<_Scalar> params;

    std::string cloud_path = "cloud.ply",
                prims_path = "primitives.bonmin.csv",
                assoc_path = "points_primitives.csv";
    _Scalar     angle_gen  = M_PI_2;
    // parse params
    {
        bool valid_input = true;

        valid_input &= pcl::console::parse_argument( argc, argv, "--scale", params.scale ) >= 0;
        valid_input &= pcl::console::parse_argument( argc, argv, "--prims", prims_path   ) >= 0;

        // cloud
        pcl::console::parse_argument( argc, argv, "--cloud", cloud_path );
        valid_input &= boost::filesystem::exists( cloud_path );

        pcl::console::parse_argument( argc, argv, "--angle-gen", angle_gen );
        pcl::console::parse_argument( argc, argv, "--adopt", params.do_adopt );
        pcl::console::parse_argument( argc, argv, "--thresh-mult", params.spatial_threshold_mult );
        pcl::console::parse_argument( argc, argv, "--assoc", assoc_path );
        pcl::console::parse_argument( argc, argv, "-a", assoc_path );

        std::cerr << "[" << __func__ << "]: " << "Usage:\t gurobi_opt --formulate\n"
                  << "\t--scale " << params.scale << "\n"
                  << "\t--prims " << prims_path << "\n"
                  << "\t--cloud " << cloud_path << "\n"
                  << "\t-a,--assoc " << assoc_path << "\n"
                  << "\t[--angle-gen " << angle_gen << "]\n"
                  << "\t[--adopt " << params.do_adopt << "]\n"
                  << "\t[--thresh-mult " << params.spatial_threshold_mult << "]\n"
                  << std::endl;

        if ( !valid_input || pcl::console::find_switch(argc,argv,"--help") || pcl::console::find_switch(argc,argv,"-h") )
        {
            std::cerr << "[" << __func__ << "]: " << "--scale, --prims are compulsory, --cloud needs to exist" << std::endl;
            return EXIT_FAILURE;
        }
    } // ... parse params

    // read primitives
    typedef std::vector<_PrimitiveT>    PatchT;
    typedef std::map   < int, PatchT >  PrimitiveMapT;
    _PrimitiveContainerT prims;
    PrimitiveMapT        prims_map;
    {
        io::readPrimitives<_PrimitiveT, PatchT>( prims, prims_path, &prims_map );
    } //...read primitives

    // Read points
    _PointContainerT points;
    {
        io::readPoints<_PointPrimitiveT>( points, cloud_path );
    }

    // Read desired angles
    processing::appendAnglesFromGenerator( params.angles, angle_gen, true );

    // associations
    std::vector<std::pair<int,int> > points_primitives;
    io::readAssociations( points_primitives, assoc_path, NULL );
    for ( size_t i = 0; i != points.size(); ++i )
    {
        // error check
        if ( i > points_primitives.size() )
        {
            std::cerr << "more points than associations..." << std::endl;
            return EXIT_FAILURE;
        }

        // store association in point
        points[i].setTag( _PointPrimitiveT::GID, points_primitives[i].first );

        // error check 2
        if ( points[i].getTag(_PointPrimitiveT::GID) == -1 )
            std::cerr << "[" << __func__ << "]: " << "point assigned to patch with id -1" << std::endl;
    }

    //____________________________WORK____________________________

    // ADOPT
    if ( params.do_adopt )
    {
        adoptPoints<GF2::MyPointPrimitiveDistanceFunctor, _PointPrimitiveT, _PrimitiveT, typename PrimitiveMapT::mapped_type::const_iterator>
                    ( points, prims_map, params.scale, params.do_adopt );
    }

    // MERGE
    std::cout << "starting mergeSameDirGids" << std::endl; fflush(stdout);
    RepresentativeSqrPatchPatchDistanceFunctorT<_Scalar, SpatialPatchPatchSingleDistanceFunctorT<_Scalar> >
            patchPatchDistFunct( params.scale * params.patch_dist_limit_mult
                               , params.angle_limit
                               , params.scale
                                 , params.patch_spatial_weight );

    PrimitiveMapT prims_map_copy = prims_map;
    PrimitiveMapT out_prims,
            *in  = &prims_map_copy,
            *out = &out_prims;
    while(
          mergeSameDirGids<_PrimitiveT, _PointPrimitiveT, typename PrimitiveMapT::mapped_type::const_iterator>
          ( *out, points, *in, params.scale, params.spatial_threshold_mult * params.scale, params.parallel_limit, patchPatchDistFunct ))
    {
        PrimitiveMapT* tmp = out;
        out = in;
        in  = tmp;
    }




    // dummy example of an iteration over all primitives
    {
        merging::RefitFunctor<_PrimitiveT> refitFunctor;
        processing::transformPrimitivesMap< _PrimitiveT
                                          , typename PrimitiveMapT::mapped_type::iterator
                                          , merging::RefitFunctor<_PrimitiveT>
                                          > ( /* [in,out] primitives: */ *out
                                            , /* [in]        functor: */ refitFunctor );
    }

    // SAVE
    std::string o_path;
    int         iteration = 0;
    {
        iteration = util::parseIteration( prims_path );
        std::stringstream ss;
        size_t it_loc = prims_path.find("_it");
        std::string fname = prims_path.substr( 0, it_loc );
        ss << fname << "_merged_it" << iteration << ".csv";
        o_path = ss.str();
        io::savePrimitives   <_PrimitiveT, typename PrimitiveMapT::mapped_type::const_iterator>( *out, o_path );
        std::cout << "wrote " << o_path << std::endl;
    }

    {
        std::stringstream ss;
        ss << "points_primitives_it" << iteration << ".csv";
        io::writeAssociations<_PointPrimitiveT>( points, ss.str() );
        std::cout << "wrote " << ss.str() << std::endl;
    }

    std::cout << "stopped mergeSameDirGids" << std::endl; fflush(stdout);
    return EXIT_SUCCESS;
}//...Merging::mergeCli()

/*! \brief Greedily assigns points with GID-s that are not in prims to prims that explain them.
*        Unambiguous assignments go through first, than based on proximity, capped by scale.
*
* \tparam _PointPrimitiveDistanceFunctor Has an eval function for a point and all primitives, to calculate the distance from point to primitive. Concept: \ref MyPointPrimitiveDistanceFunctor.
* \tparam _PointPrimitiveT     Wraps a point, exposing pos() and dir() functions. Concept: \ref GF2::PointPrimitive.
* \tparam _PrimitiveT          Wraps a primitive, exposing pos() and dir() functions. Concept: \ref GF2::LinePrimitive2.
* \tparam _PointContainerT     Holds the points. Concept: std::vector< \ref GF2::PointPrimitive >.
* \tparam _PrimitiveContainerT Holds the primitives grouped by GID in a two level structure. Concept: std::map< int, std::vector< \ref GF2::LinePrimitive2 > >.
* \tparam _Scalar              Floating point precision of primitives, points, etc. Concept: \ref GF2::PointPrimitive::Scalar.
* \param[in,out] points        Contains the points, some assigned, some to be assigned to the primitives in prims.
* \param[in]     prims         Contains some primitives tagged with GID and DIR_GID. GID defines the assignment between points and primitives.
* \param[in]     scale         Distance threshold parameter.
* \param[in]     mode          1: re-assign un-ambiguous points (1 adopter); 2: first re-assign unambiguous, then closest, if inside explaining primitive's scale.
*/
template < class _PointPrimitiveDistanceFunctor
         , class _PointPrimitiveT
         , class _PrimitiveT
         , class _inner_const_iterator
         , class _PointContainerT
         , class _PrimitiveContainerT
         , typename _Scalar >
int Merging::adoptPoints( _PointContainerT          & points
                        , _PrimitiveContainerT const& prims
                        , _Scalar              const  scale
                        , char                 const  mode )
{
    //typedef GF2::MyPointPrimitiveDistanceFunctor _PointPrimitiveDistanceFunctor;
    typedef typename _PrimitiveContainerT::const_iterator             outer_const_iterator;
    //typedef typename outer_const_iterator::value_type::const_iterator inner_const_iterator;

    int err = EXIT_SUCCESS;

    // select unassigned points
    std::deque<int> orphan_pids;
    {
        for ( size_t pid = 0; pid != points.size(); ++pid )
        {
            int gid = points[pid].getTag( _PointPrimitiveT::GID );
            if (    (prims.find(gid) == prims.end())
                 || (!containers::valueOf<_PrimitiveT>(prims.find(gid)).size()) )
            //if ( !prims.at(gid).size() ) // no primitives with this GID
            {
                points[pid].setTag   ( _PointPrimitiveT::GID, -2 );
                orphan_pids.push_back( pid );
            }
        }
    }

    int change = 0, iteration = 0;
    do
    {
        std::cout << "[" << __func__ << "]: " << "iteration " << iteration << std::endl;
        // reset re-assignment counter
        change = 0;

        // store possible primitives to assign points[pid] to
        typedef std::pair<int,int> GidLid1;
        std::map<int, std::vector<GidLid1> > adopter_gids; // < pid, [ <gid,lid1>, ... ] >
        {
            // iterate over unassigned points
            std::deque<int>::const_iterator points_it_end = orphan_pids.end();
            for ( std::deque<int>::const_iterator points_it = orphan_pids.begin(); points_it != points_it_end; ++points_it )
            {
                // store point id
                const int pid = *points_it;

                // for patches
                //typename PrimitiveMapT::const_iterator end_it = prims.end();
                outer_const_iterator end_it = prims.end();
                for ( outer_const_iterator it = prims.begin(); it != end_it; ++it )
                {
                    // record primitive id in patch
                    int lid1 = 0;
                    // for primitives in patch
                    _inner_const_iterator end_it2 = it->second.end();
                    for ( _inner_const_iterator it2 = it->second.begin(); it2 != end_it2; ++it2, ++lid1 )
                    {
                        // distance from point to primitive
                        _Scalar dist = _PointPrimitiveDistanceFunctor::template eval<_Scalar>( points[pid], *it2 );
                        // store if inside scale
                        if ( dist < scale )
                        {
                            adopter_gids[pid].push_back( std::pair<int,int>(it->first,lid1) );
                        } //...if dist

                    } //...for prims_in_patch
                } //...for patches
            } //...for points
        } //...adopters

        // reassign points
        if ( iteration == 0 ) //
        {
            for ( std::deque<int>::iterator points_it = orphan_pids.begin(); points_it != orphan_pids.end(); /*nothing*/ )
            {
                // store point id
                const int pid = *points_it;
                //std::cout << "[" << __func__ << "]: " << "point " << pid << " have " << adopter_gids[pid].size() << " adopters" << std::endl;

                if ( !adopter_gids[pid].size() ) std::cerr << "[" << __func__ << "]: " << "point " << pid << " seems to be an outlier...no points could adopt it..." << std::endl;

                // if point has only one possible reassignment - do assign it
                if ( adopter_gids[pid].size() == 1 )
                {
                    points[pid].setTag( _PointPrimitiveT::GID, adopter_gids[pid][0].first );
                    points_it = orphan_pids.erase( points_it ); // returns iterator to next element
                    ++change;
                }
                else
                {
                    ++points_it; // increment only, if no erase has been done
                }
            }
        }
        else if ( mode == 2 )
        {
            int closest_gid = -3; // gid of point, that is closest to an orphan, termination crit
            do
            {
                // reset
                closest_gid = -3;
                _Scalar                   closest_distance  = std::numeric_limits<_Scalar>::max();
                std::deque<int>::iterator closest_orphan    = orphan_pids.begin();

                for ( std::deque<int>::iterator points_it = orphan_pids.begin(); points_it != orphan_pids.end(); ++points_it )
                {
                    // store point id
                    const int pid = *points_it; // orphan_pid

                    // look for the closest assigned point from possible adopters
                    for ( int pid2 = 0; pid2 != points.size(); ++pid2 )
                    {
                        // rule out itself
                        if ( pid2 == pid ) continue;
                        // rule out other orphans
                        const int gid2 = points[pid2].getTag(_PointPrimitiveT::GID);
                        if ( gid2 < 0 )  continue;

                        _Scalar dist = (points[pid].template pos() - points[pid2].template pos()).norm();
                        // store if closer, than earlier orphan, and if inside scale to given primitive (can be explained)
                        if ( (dist < closest_distance) && _PointPrimitiveDistanceFunctor::template eval<_Scalar>(points[pid], prims.at(gid2).at(0)) )
                        {
                            if ( prims.at(gid2).size() > 1 ) std::cerr << "[" << __func__ << "]: " << "two lines in one patch, not prepared here, fix this" << std::endl;

                            closest_distance = dist;
                            closest_gid      = points[pid2].getTag( _PointPrimitiveT::GID );
                            closest_orphan   = points_it;
                        } //...if dist <
                    } //...for cloud
                } //...for all orphans

                // check, if any luck
                if ( closest_gid >= 0 )
                {
                    points[ *closest_orphan ].setTag( _PointPrimitiveT::GID, closest_gid );
                    std::cout << "erasing orphan " << std::distance( orphan_pids.begin(), closest_orphan ) << "/" << orphan_pids.size();
                    orphan_pids.erase( closest_orphan );
                    std::cout << " size now " << orphan_pids.size() << std::endl;
                    ++change;
                }
            } while ( (closest_gid >= 0) && orphan_pids.size() );

        } //...if iteration != 0

        ++iteration;
    } while ( change ); // stop, if no new points were reassigned

    return err;
} //...adoptPoints()

/*! \brief Decides, if two patches are adjacent and have the same direction. Used in \ref erging::mergeSameDirGids().
 *
 */
//template <typename _Scalar>
//inline bool decide_merge( _Scalar min_dist, _Scalar threshold, _Scalar angle, _Scalar parallel_limit )
//{
//    return (min_dist < threshold) && (angle < parallel_limit);
//} //...decide_merge

template <class _PrimitiveT, typename _Scalar>
//inline _Scalar segmentDistance(
inline bool decide_merge(
        std::vector<Eigen::Matrix<_Scalar,3,1> > const& extrema0
        , _PrimitiveT const& l0
        , std::vector<Eigen::Matrix<_Scalar,3,1> > const& extrema1
        , _PrimitiveT const& l1
        , _Scalar scale)
{

    std::cout << "testing (" << l0.getTag(_PrimitiveT::GID )     << ","
                             << l0.getTag(_PrimitiveT::DIR_GID ) << ")"
              << " vs. ("    << l1.getTag(_PrimitiveT::GID ) << ","
                             << l1.getTag(_PrimitiveT::DIR_GID ) << ")\t" << std::endl;

//    bool print = (l0.getTag(_PrimitiveT::GID) == 0 && l1.getTag(_PrimitiveT::GID) == 3 ) ||
//                 (l0.getTag(_PrimitiveT::GID) == 3 && l1.getTag(_PrimitiveT::GID) == 0 );
//    if (print)
//        std::cout<< "Let's go ! " << std::endl;

    // We have two lines l0 and l1, respectively defined by
    // l0a-l0b and l1a-l1b
    //  A. Check if both lines have the same direction id. If not,
    //      1. We project l0a and l0b to l1 (orthogonally to l1), and compute the norm of l0a-proj(l0a,l1) and l0b-proj(l0b,l1)
    //      2. Compute the other way
    //      3. Check if the two lines are almost aligned (all norms are smaller than the scale parameters)
    //  B. If one way is correct, project second line end points to the first one
    //     and check at least one end point is projected the segment
    const Eigen::Matrix<_Scalar,3,1> & l0a = extrema0[0];
    const Eigen::Matrix<_Scalar,3,1> & l0b = extrema0[1];
    const Eigen::Matrix<_Scalar,3,1> & l1a = extrema1[0];
    const Eigen::Matrix<_Scalar,3,1> & l1b = extrema1[1];

    const Eigen::Matrix<_Scalar,3,1> n0 = l0.template normal<_Scalar>();

    //const _Scalar sqScale = scale*scale;
    //const _Scalar l0SqLengthAndScale = (l0b-l0a).squaredNorm() + scale*scale;

    bool sameTag = l0.getTag(_PrimitiveT::DIR_GID ) == l1.getTag(_PrimitiveT::DIR_GID );

    // check if l1 is aligned to l0
    if ( sameTag || // exactly aligned
         ( std::abs(n0.dot(l1a-l0a)) <= scale && // check l1a-proj(l1a,l0) <= scale
           std::abs(n0.dot(l1b-l0a)) <= scale)){  // check l1b-proj(l1b,l0) <= scale

        // check if at least one l1 endpoint is projected onto l0
        const Eigen::Matrix<_Scalar,3,1> l0dir = (l0b - l0a).normalized();
        const _Scalar dl0  = (l0b - l0a).norm() + scale;
        const _Scalar dl1a = l0dir.dot(l1a-l0a);
        const _Scalar dl1b = l0dir.dot(l1b-l0a);

        if ((dl1a >= -scale && dl1a <= dl0) ||
            (dl1b >= -scale && dl1b <= dl0))
                return true;
    }

    const Eigen::Matrix<_Scalar,3,1> n1 = l1.template normal<_Scalar>();

    // check if l0 is aligned to l1
    if ( sameTag ||
         ( std::abs(n1.dot(l0a-l1a)) <= scale &&  // check l0a-proj(l0a,l0) <= scale
           std::abs(n1.dot(l0b-l1a)) <= scale )){ // check l0b-proj(l0b,l0) <= scale

        // check if at least one l0 endpoint is projected onto l1
        const Eigen::Matrix<_Scalar,3,1> l1dir = (l1b - l1a).normalized();
        const _Scalar dl1  = (l1b - l1a).norm() + scale;
        const _Scalar dl0a = l1dir.dot(l0a-l1a);
        const _Scalar dl0b = l1dir.dot(l0b-l1a);

        if ((dl0a >= -scale && dl0a <= dl1) ||
            (dl0b >= -scale && dl0b <= dl1))
                return true;
    }

    return false;
}



template <class Container,
          class PrimitiveT>
inline void merge( Container& out_primitives,
                   const PrimitiveT& l0,
                   const PrimitiveT& l1){

    std::cout << "merging (" << l0.getTag(PrimitiveT::GID )     << ","
                             << l0.getTag(PrimitiveT::DIR_GID ) << ")"
              << " and ("    << l1.getTag(PrimitiveT::GID ) << ","
                             << l1.getTag(PrimitiveT::DIR_GID ) << ")\t" << std::endl;

    // for now, do nothing except duplicating the two primitives

    containers::add<PrimitiveT>( out_primitives, l0.getTag(PrimitiveT::GID ), l0 );
    containers::add<PrimitiveT>( out_primitives, l1.getTag(PrimitiveT::GID ), l1 );
}

/*! \brief Merges adjacent patches that have the same direction ID or are almost parallel.
 *
 *  \tparam _PatchPatchDistanceFunctorT  Concept: \ref GF2::RepresentativeSqrPatchPatchDistanceFunctorT.
 *  \param[in] patchPatchDistFunct       Distance functor between two patches, to define adjacency.
 *  \param[in] spatial_threshold         Two extrema should be at least this close to be merged. Concept: \ref MergeParams::spatial_threshold_mult == 3 * scale.
 *  \sa #decide_merge()
 */
template < class    _PrimitiveT
         , class    _PointPrimitiveT
         , class    _inner_const_iterator
         , class    _PrimitiveContainerT
         , class    _PointContainerT
         , typename _Scalar
         , class    _PatchPatchDistanceFunctorT>
int Merging::mergeSameDirGids( _PrimitiveContainerT             & out_primitives
                             , _PointContainerT                 & points
                             , _PrimitiveContainerT        const& primitives
                             , _Scalar                     const  scale
                             , _Scalar                     const  spatial_threshold
                             , _Scalar                     const  parallel_limit
                             , _PatchPatchDistanceFunctorT const& patchPatchDistFunct )
{
    typedef typename _PrimitiveContainerT::const_iterator      outer_const_iterator;
    typedef           std::vector<Eigen::Matrix<_Scalar,3,1> > ExtremaT;
    typedef           std::map   < int, ExtremaT>              LidExtremaT;
    typedef           std::map   < int, LidExtremaT >          GidLidExtremaT;
    typedef           std::pair  < int, // map key
                                   int> // linear index in the array associated to the key
                      GidLid;

    int err = EXIT_SUCCESS;

    // Populations
    GidPidVectorMap populations; // populations[gid] == std::vector<int> {pid0,pid1,...}
    if ( EXIT_SUCCESS == err )
    {
        err = processing::getPopulations( populations, points );
        CHECK( err, "getPopulations" );
    }

    // Extrema
    GidLidExtremaT extrema; // <gid,lid> -> vector<x0, x1, ...>
    if ( EXIT_SUCCESS == err )
    {
        // for all patches
        for ( outer_const_iterator outer_it  = primitives.begin();
                                  (outer_it != primitives.end())  && (EXIT_SUCCESS == err);
                                 ++outer_it )
        {
            int gid  = -2; // (-1 is "unset")
            int lid = 0; // linear index of primitive in container (to keep track)
            // for all directions
            for ( _inner_const_iterator inner_it  = containers::valueOf<_PrimitiveT>(outer_it).begin();
                                       (inner_it != containers::valueOf<_PrimitiveT>(outer_it).end()) && (EXIT_SUCCESS == err);
                                      ++inner_it, ++lid )
            {
                // save patch gid
                if ( gid == -2 )
                {
                    gid = inner_it->getTag( _PrimitiveT::GID );
                    // sanity check
                    if ( extrema.find(gid) != extrema.end() )
                    {
                        std::cerr << "[" << __func__ << "]: " << "GID not unique for patch...:-S" << std::endl;
                    }
                }

                err = inner_it->template getExtent<_PointPrimitiveT>
                                                           ( extrema[gid][lid]
                                                           , &points
                                                           , scale
                                                           , populations[gid].size() ? &(populations[gid]) : NULL );
            } //...for primitives
        } //...for patches

        CHECK( err, "calcExtrema" )
    } //...getExtrema

    bool needOtherLoop = false;

    // while (needOtherLoop){
    // needOtherLoop = false;

    // Store the primitives that have been matched
    // First  (key)   = candidate,
    // Second (value) = reference.
    typedef std::set< GidLid > AliasesT;
    AliasesT aliases;

    typedef typename GidLidExtremaT::const_iterator GidIt;
    typedef typename LidExtremaT::const_iterator    PrimIt;

    // Here are two first loops to iterate over the map entries, and for each of them over the
    // linear array containing primitives (we call them ref. primitive in the following).
    //
    // For each of these ref. primitives, we check for merging candidates. They can be in:
    //   - the same map entry, after the ref. primitive in the linear Primitive array
    //   - the next map entries, any index
    //
    // For each couple ref/candidate, we check if we can merge. If yes, we do it and then invalidate
    // both the ref and the candidate to prevent to merge them with other primitives. Indeed, the
    // merging process can potentially remove the primitives, or at least change their properties.
    // Primitives are invalidated by storing them as key in the aliases structure.
    //
    // After processing, the aliases set contains for any candidate described by a GidLid:
    //   aliases [candGidLid];
    //
    // This structure is read later to detect the primitives that have not been merged (not present
    // as key)

    // Reference traversal
    for ( GidIt gid_it = extrema.cbegin(); gid_it != extrema.cend(); ++gid_it )
    {
        const int gid0 = gid_it->first;

        for ( PrimIt prim_it = gid_it->second.cbegin(); prim_it != gid_it->second.cend(); ++prim_it )
        {
            // linear id of the reference
            const int lid0 = std::distance<typename LidExtremaT::const_iterator>( gid_it ->second.cbegin(), prim_it );

            // Reference primitive key, used to check if it has not been merged previously
            GidLid refGidLid (gid0, lid0);

            // check if this primitives has not been merged previously
            if (aliases.find(refGidLid) != aliases.end()) continue;

            // reference primitive
            const _PrimitiveT& prim0 = primitives.at(gid0).at(lid0);

            // is the reference valid, i.e. no merge occured
            bool is0Valid = true;


            // Candidates traversal
            // This loop and the nested one check if the ref primitive is still valid for comparison,
            // i.e. no merge occured.
            // This construction prevent to use two successive break commands (this loop and the nested one)
            for ( GidIt gid_it1 = gid_it; is0Valid && gid_it1 != extrema.cend(); ++gid_it1 )
            {

                int gid1 = gid_it1->first;

                // Given the ref. primitive, we check for merging candidates. They can be in:
                //   - the same map entry, after the ref. primitive in the linear Primitive array
                //   - the next map entries, any index
                for ( PrimIt prim_it1 = (gid_it1 == gid_it) ? ++PrimIt(prim_it) :       // for the same map entry, after the ref. primitive
                                                              gid_it1->second.cbegin(); // for the next map entries, any index
                      is0Valid && prim_it1 != gid_it1->second.cend();
                      ++prim_it1 )
                {

                    const int lid1 = std::distance<typename LidExtremaT::const_iterator>( gid_it1->second.cbegin(), prim_it1 );

                    // resume of the candidate primitive, used to record the matching operation for later and
                    // detect previous merge operations
                    GidLid candGidLid (gid1, lid1);

                    // Here we don't need to define
                    // bool is1Valid,
                    // calling continue is sufficient to jump to the next primitive after and merge,
                    // plus here check that a previous merge has not been recorded
                    if (aliases.find(candGidLid) != aliases.end()) continue;

                    const _PrimitiveT& prim1 = primitives.at(gid1).at(lid1);

                    if (decide_merge( prim_it->second,  // extrema 0
                                      prim0,            // prim 0
                                      prim_it1->second, // extrema 1
                                      prim1,            // prim 1
                                      scale ))
                    {
                        std::cout << " YES" << std::endl;

                        // record this to detect unmerged primitives later and invalidate both primitives
                        aliases.insert(candGidLid);
                        aliases.insert(refGidLid);

                        merge( out_primitives, // [out] Container storing merged primitives
                               prim0,          // [in]  First primitive
                               prim1);         // [in]  Second primitive

                        is0Valid = false;
                        continue;  // jump to the next couple ref/candidate primitive
                    }
                    else
                        std::cout << " NO" << std::endl;
                }
            }

            // if we reach that case, we know that this primitive will not been visited anymore,
            // so it can be add to the out_primitive container
            if (is0Valid){
                containers::add<_PrimitiveT>( out_primitives, gid0, prim0 );

                std::cout << "store remaining (" << prim0.getTag(_PrimitiveT::GID )     << ","
                          << prim0.getTag(_PrimitiveT::DIR_GID ) << ")t" << std::endl;
            }
        }
    }

//    cout << "[in]:  " << primitives.size() << endl;
//    for ( outer_const_iterator outer_it  = primitives.begin();
//          (outer_it != primitives.end());
//          ++outer_it )
//        //std::cout << (*outer_it).second.size() << std::endl;
//        cout << (*outer_it).second.at(0).getTag(_PrimitiveT::GID ) << " - " << (*outer_it).second.at(0).getTag(_PrimitiveT::DIR_GID ) << endl;

//    cout << "[out]: " << out_primitives.size() << endl;
//    for ( outer_const_iterator outer_it  = out_primitives.begin();
//          (outer_it != out_primitives.end()) ;
//          ++outer_it )
//        //std::cout << (*outer_it).second.size() << std::endl;
//        cout << (*outer_it).second.at(0).getTag(_PrimitiveT::GID ) << " - " << (*outer_it).second.at(0).getTag(_PrimitiveT::DIR_GID ) << endl;


    // debug
//    std::ofstream dbg( "lines.plot" );
//    for ( auto it = extrema.begin(); it != extrema.end(); ++it )
//    {
//        for ( int i = 0; i != it->second.size(); ++i )
//        {
//            dbg << extrema[it->first][i][0].transpose() << "\n"
//                << extrema[it->first][i][1].transpose() << "\n\n";
//        }
//    }
//    dbg.close();
    return needOtherLoop;
} //...Merging::mergeSameDirGids()

} //...namespace GF2

#undef CHECK

#endif // GF2_MERGING_HPP
