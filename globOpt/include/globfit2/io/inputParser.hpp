/**
  * inputParser.hpp
  * : Class name (if applicable)
  * Author: Aron Monszpart <a.monszpart@cs.ucl.ac.uk>
  * Created: 10/11/2014
  */

#ifndef INPUTPARSER_HPP
#define INPUTPARSER_HPP

template < class _InnerPrimitiveContainerT
         , class _PclCloudT
         , class _PointContainerT
         , class _PrimitiveContainerT
         , class _PrimitiveMapT
         , typename _Scalar >
inline int parseInput( _PointContainerT         &points
                     , typename _PclCloudT::Ptr &pcl_cloud
                     , _PrimitiveContainerT     &initial_primitives
                     , _PrimitiveMapT           &patches
                     , RansacParams<_Scalar>    &params
                     , int                       argc, char **argv
                     , bool                      read_assoc = true )
{
    typedef typename _PointContainerT::value_type           PointPrimitiveT;
    typedef typename _InnerPrimitiveContainerT::value_type  PrimitiveT;

    bool valid_input = true;
    std::string cloud_path, input_prims_path, associations_path;

    if (    (GF2::console::parse_argument( argc, argv, "--cloud", cloud_path) < 0)
         && (!boost::filesystem::exists(cloud_path)) )
    {
        std::cerr << "[" << __func__ << "]: " << "--cloud does not exist: " << cloud_path << std::endl;
        valid_input = false;
    }

    // primitives
    if (    (GF2::console::parse_argument( argc, argv, "-p"     , input_prims_path) < 0)
         && (GF2::console::parse_argument( argc, argv, "--prims", input_prims_path) < 0)
         && (!boost::filesystem::exists(input_prims_path)) )
    {
        std::cerr << "[" << __func__ << "]: " << "-p or --prims is compulsory" << std::endl;
        valid_input = false;
    }

    if (    (pcl::console::parse_argument( argc, argv, "-a", associations_path) < 0)
         && (pcl::console::parse_argument( argc, argv, "--assoc", associations_path) < 0)
         && (!boost::filesystem::exists(associations_path)) )
    {
        if ( read_assoc )
        {
            std::cerr << "[" << __func__ << "]: " << "-a or --assoc is compulsory" << std::endl;
            valid_input = false;
        }
    }

    // scale
    if (    (GF2::console::parse_argument( argc, argv, "--scale", params.scale) < 0)
         && (GF2::console::parse_argument( argc, argv, "-sc"    , params.scale) < 0) )
    {
        std::cerr << "[" << __func__ << "]: " << "--scale is compulsory" << std::endl;
        valid_input = false;
    }

    // READ
    int err     = EXIT_SUCCESS;
    pcl_cloud   = typename _PclCloudT::Ptr( new _PclCloudT() );
    if ( EXIT_SUCCESS == err )
    {
        err = GF2::io::readPoints<PointPrimitiveT>( points, cloud_path, &pcl_cloud );
        if ( err != EXIT_SUCCESS )
        {
            std::cerr << "[" << __func__ << "]: " << "readPoints returned error " << err << std::endl;
            valid_input = false;
        }
    } //...read points

    if ( EXIT_SUCCESS == err )
    {
        std::cout << "[" << __func__ << "]: " << "reading primitives from " << input_prims_path << "...";
        err = GF2::io::readPrimitives<PrimitiveT, _InnerPrimitiveContainerT>( initial_primitives, input_prims_path, &patches );
        if ( EXIT_SUCCESS == err )
            std::cout << "[" << __func__ << "]: " << "reading primitives ok (#: " << initial_primitives.size() << ")\n";
        else
            std::cerr << "[" << __func__ << "]: " << "reading primitives failed" << std::endl;
    } //...read primitives

    // read assoc
    if ( read_assoc )
    {
        std::vector< std::pair<int,int> > points_primitives;
        GF2::io::readAssociations( points_primitives, associations_path, NULL );
        for ( size_t i = 0; i != points.size(); ++i )
        {
            // store association in point
            points[i].setTag( PointPrimitiveT::GID, points_primitives[i].first );
        }
    }

    return err + !valid_input;
}


#endif // INPUTPARSER_HPP