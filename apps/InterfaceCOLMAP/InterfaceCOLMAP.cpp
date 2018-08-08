/*
 * InterfaceCOLMAP.cpp
 *
 * Copyright (c) 2014-2018 SEACAVE
 *
 * Author(s):
 *
 *      cDc <cdc.seacave@gmail.com>
 *
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Additional Terms:
 *
 *      You are required to preserve legal notices and author attributions in
 *      that material or in the Appropriate Legal Notices displayed by works
 *      containing it.
 */

#include "../../libs/MVS/Common.h"
#include "../../libs/MVS/Scene.h"
#define _USE_OPENCV
#include "../../libs/MVS/Interface.h"
#include <boost/program_options.hpp>

using namespace MVS;


// D E F I N E S ///////////////////////////////////////////////////

#define APPNAME _T("InterfaceCOLMAP")
#define MVS_EXT _T(".mvs")
#define COLMAP_IMAGES_FOLDER _T("images/")
#define COLMAP_SPARSE_FOLDER _T("sparse/")
#define COLMAP_CAMERAS COLMAP_SPARSE_FOLDER _T("cameras.txt")
#define COLMAP_IMAGES COLMAP_SPARSE_FOLDER _T("images.txt")
#define COLMAP_POINTS COLMAP_SPARSE_FOLDER _T("points3D.txt")
#define COLMAP_STEREO_FOLDER _T("stereo/")
#define COLMAP_FUSION COLMAP_STEREO_FOLDER _T("fusion.cfg")
#define COLMAP_PATCHMATCH COLMAP_STEREO_FOLDER _T("patch-match.cfg")
#define COLMAP_STEREO_CONSISTENCYGRAPHS_FOLDER COLMAP_STEREO_FOLDER _T("consistency_graphs/")
#define COLMAP_STEREO_DEPTHMAPS_FOLDER COLMAP_STEREO_FOLDER _T("depth_maps/")
#define COLMAP_STEREO_NORMALMAPS_FOLDER COLMAP_STEREO_FOLDER _T("normal_maps/")


// S T R U C T S ///////////////////////////////////////////////////

namespace OPT {
bool b3Dnovator2COLMAP; // conversion direction
bool bNormalizeIntrinsics;
String strInputFileName;
String strOutputFileName;
String strImageFolder;
unsigned nArchiveType;
int nProcessPriority;
unsigned nMaxThreads;
String strConfigFileName;
boost::program_options::variables_map vm;
} // namespace OPT

// initialize and parse the command line parameters
bool Initialize(size_t argc, LPCTSTR* argv)
{
	// initialize log and console
	OPEN_LOG();
	OPEN_LOGCONSOLE();

	// group of options allowed only on command line
	boost::program_options::options_description generic("Generic options");
	generic.add_options()
		("help,h", "produce this help message")
		("working-folder,w", boost::program_options::value<std::string>(&WORKING_FOLDER), "working directory (default current directory)")
		("config-file,c", boost::program_options::value<std::string>(&OPT::strConfigFileName)->default_value(APPNAME _T(".cfg")), "file name containing program options")
		("archive-type", boost::program_options::value<unsigned>(&OPT::nArchiveType)->default_value(2), "project archive type: 0-text, 1-binary, 2-compressed binary")
		("process-priority", boost::program_options::value<int>(&OPT::nProcessPriority)->default_value(-1), "process priority (below normal by default)")
		("max-threads", boost::program_options::value<unsigned>(&OPT::nMaxThreads)->default_value(0), "maximum number of threads (0 for using all available cores)")
		#if TD_VERBOSE != TD_VERBOSE_OFF
		("verbosity,v", boost::program_options::value<int>(&g_nVerbosityLevel)->default_value(
			#if TD_VERBOSE == TD_VERBOSE_DEBUG
			3
			#else
			2
			#endif
			), "verbosity level")
		#endif
		;

	// group of options allowed both on command line and in config file
	boost::program_options::options_description config("Main options");
	config.add_options()
		("input-file,i", boost::program_options::value<std::string>(&OPT::strInputFileName), "input COLMAP folder containing cameras, images and points files OR input MVS project file")
		("output-file,o", boost::program_options::value<std::string>(&OPT::strOutputFileName), "output filename for storing the MVS project")
		("image-folder", boost::program_options::value<std::string>(&OPT::strImageFolder)->default_value(COLMAP_IMAGES_FOLDER), "folder to the undistorted images")
		("normalize,f", boost::program_options::value<bool>(&OPT::bNormalizeIntrinsics)->default_value(true), "normalize intrinsics while exporting to MVS format")
		;

	boost::program_options::options_description cmdline_options;
	cmdline_options.add(generic).add(config);

	boost::program_options::options_description config_file_options;
	config_file_options.add(config);

	boost::program_options::positional_options_description p;
	p.add("input-file", -1);

	try {
		// parse command line options
		boost::program_options::store(boost::program_options::command_line_parser((int)argc, argv).options(cmdline_options).positional(p).run(), OPT::vm);
		boost::program_options::notify(OPT::vm);
		INIT_WORKING_FOLDER;
		// parse configuration file
		std::ifstream ifs(MAKE_PATH_SAFE(OPT::strConfigFileName));
		if (ifs) {
			boost::program_options::store(parse_config_file(ifs, config_file_options), OPT::vm);
			boost::program_options::notify(OPT::vm);
		}
	}
	catch (const std::exception& e) {
		LOG(e.what());
		return false;
	}

	// initialize the log file
	OPEN_LOGFILE(MAKE_PATH(APPNAME _T("-")+Util::getUniqueName(0)+_T(".log")));

	// print application details: version and command line
	Util::LogBuild();
	LOG(_T("Command line: ") APPNAME _T("%s"), Util::CommandLineToString(argc, argv).c_str());

	// validate input
	Util::ensureValidPath(OPT::strInputFileName);
	const String strInputFileNameExt(Util::getFileExt(OPT::strInputFileName).ToLower());
	OPT::b3Dnovator2COLMAP = (strInputFileNameExt == MVS_EXT);
	const bool bInvalidCommand(OPT::strInputFileName.empty());
	if (OPT::vm.count("help") || bInvalidCommand) {
		boost::program_options::options_description visible("Available options");
		visible.add(generic).add(config);
		GET_LOG() << visible;
	}
	if (bInvalidCommand)
		return false;

	// initialize optional options
	Util::ensureValidFolderPath(OPT::strImageFolder);
	Util::ensureValidPath(OPT::strOutputFileName);
	if (OPT::b3Dnovator2COLMAP) {
		if (OPT::strOutputFileName.empty())
			OPT::strOutputFileName = Util::getFilePath(OPT::strInputFileName);
		Util::ensureFolderSlash(OPT::strOutputFileName);
	} else {
		Util::ensureFolderSlash(OPT::strInputFileName);
		if (OPT::strOutputFileName.empty())
			OPT::strOutputFileName = OPT::strInputFileName + _T("scene") MVS_EXT;
		else
			OPT::strImageFolder = Util::getRelativePath(Util::getFilePath(OPT::strOutputFileName), OPT::strInputFileName+OPT::strImageFolder);
	}

	// initialize global options
	Process::setCurrentProcessPriority((Process::Priority)OPT::nProcessPriority);
	#ifdef _USE_OPENMP
	if (OPT::nMaxThreads != 0)
		omp_set_num_threads(OPT::nMaxThreads);
	#endif

	#ifdef _USE_BREAKPAD
	// start memory dumper
	MiniDumper::Create(APPNAME, WORKING_FOLDER);
	#endif
	return true;
}

// finalize application instance
void Finalize()
{
	#if TD_VERBOSE != TD_VERBOSE_OFF
	// print memory statistics
	Util::LogMemoryInfo();
	#endif

	CLOSE_LOGFILE();
	CLOSE_LOGCONSOLE();
	CLOSE_LOG();
}

namespace COLMAP {
// tools
bool NextLine(std::istream& stream, std::istringstream& in, bool bIgnoreEmpty=true) {
	String line;
	do {
		std::getline(stream, line);
		Util::strTrim(line, _T(" "));
		if (stream.fail())
			return false;
	} while (((bIgnoreEmpty && line.empty()) || line[0] == '#') && stream.good());
	in.clear();
	in.str(line);
	return true;
}
// structure describing a camera
struct Camera {
	uint32_t ID; // ID of the camera
	String model; // camera model name
	uint32_t width, height; // camera resolution
	std::vector<REAL> params; // camera parameters

	Camera() {}
	Camera(uint32_t _ID) : ID(_ID) {}
	bool operator < (const Camera& rhs) const { return ID < rhs.ID; }

	struct CameraHash {
		size_t operator()(const Camera& camera) const {
			const size_t h1(std::hash<String>()(camera.model));
			const size_t h2(std::hash<uint32_t>()(camera.width));
			const size_t h3(std::hash<uint32_t>()(camera.height));
			size_t h(h1 ^ ((h2 ^ (h3 << 1)) << 1));
			for (REAL p: camera.params)
				h = std::hash<REAL>()(p) ^ (h << 1);
			return h;
		}
	};
	struct CameraEqualTo {
		bool operator()(const Camera& _Left, const Camera& _Right) const {
			return _Left.model == _Right.model &&
				_Left.width == _Right.width && _Left.height == _Right.height &&
				_Left.params == _Right.params;
		}
	};

	// Camera list with one line of data per camera:
	//   CAMERA_ID, MODEL, WIDTH, HEIGHT, PARAMS[]
	bool Read(std::istream& stream) {
		std::istringstream in;
		if (!NextLine(stream, in))
			return false;
		in >> ID >> model >> width >> height;
		if (in.fail())
			return false;
		if (model != _T("PINHOLE"))
			return false;
		params.resize(4);
		in >> params[0] >> params[1] >> params[2] >> params[3];
		return !in.fail();
	}
	bool Write(std::ostream& out) const {
		out << ID+1 << _T(" ") << model << _T(" ") << width << _T(" ") << height;
		if (out.fail())
			return false;
		for (REAL param: params) {
			out << _T(" ") << param;
			if (out.fail())
				return false;
		}
		out << std::endl;
		return true;
	}
};
typedef std::vector<Camera> Cameras;
// structure describing an image
struct Image {
	struct Proj {
		Eigen::Vector2f p;
		uint32_t idPoint;
	};
	uint32_t ID; // ID of the image
	Eigen::Quaterniond q; // rotation
	Eigen::Vector3d t; // translation
	uint32_t idCamera; // ID of the associated camera
	String name; // image file name
	std::vector<Proj> projs; // known image projections

	Image() {}
	Image(uint32_t _ID) : ID(_ID) {}
	bool operator < (const Image& rhs) const { return ID < rhs.ID; }

	// Image list with two lines of data per image:
	//   IMAGE_ID, QW, QX, QY, QZ, TX, TY, TZ, CAMERA_ID, NAME
	//   POINTS2D[] as (X, Y, POINT3D_ID)
	bool Read(std::istream& stream) {
		std::istringstream in;
		if (!NextLine(stream, in))
			return false;
		in  >> ID
			>> q.w() >> q.x() >> q.y() >> q.z()
			>> t(0) >> t(1) >> t(2)
			>> idCamera >> name;
		if (in.fail())
			return false;
		Util::ensureValidPath(name);
		if (!NextLine(stream, in, false))
			return false;
		projs.clear();
		while (true) {
			Proj proj;
			in >> proj.p(0) >> proj.p(1) >> (int&)proj.idPoint;
			if (in.fail())
				break;
			projs.push_back(proj);
		}
		return true;
	}
	bool Write(std::ostream& out) const {
		ASSERT(!projs.empty());
		out << ID+1 << _T(" ")
			<< q.w() << _T(" ") << q.x() << _T(" ") << q.y() << _T(" ") << q.z() << _T(" ")
			<< t(0) << _T(" ") << t(1) << _T(" ") << t(2) << _T(" ")
			<< idCamera+1 << _T(" ") << name
			<< std::endl;
		for (const Proj& proj: projs) {
			out << proj.p(0) << _T(" ") << proj.p(1) << _T(" ") << (int)proj.idPoint+1 << _T(" ");
			if (out.fail())
				return false;
		}
		out << std::endl;
		return !out.fail();
	}
};
typedef std::vector<Image> Images;
// structure describing a 3D point
struct Point {
	struct Track {
		uint32_t idImage;
		uint32_t idProj;
	};
	uint32_t ID; // ID of the point
	Interface::Pos3f p; // position
	Interface::Col3 c; // BGR color
	float e; // error
	std::vector<Track> tracks; // point track

	Point() {}
	Point(uint32_t _ID) : ID(_ID) {}
	bool operator < (const Image& rhs) const { return ID < rhs.ID; }

	// 3D point list with one line of data per point:
	//   POINT3D_ID, X, Y, Z, R, G, B, ERROR, TRACK[] as (IMAGE_ID, POINT2D_IDX)
	bool Read(std::istream& stream) {
		std::istringstream in;
		if (!NextLine(stream, in))
			return false;
		int r,g,b;
		in  >> ID
			>> p.x >> p.y >> p.z
			>> r >> g >> b
			>> e;
		c.x = CLAMP(b,0,255);
		c.y = CLAMP(g,0,255);
		c.z = CLAMP(r,0,255);
		if (in.fail())
			return false;
		tracks.clear();
		while (true) {
			Track track;
			in >> track.idImage >> track.idProj;
			if (in.fail())
				break;
			tracks.push_back(track);
		}
		return !tracks.empty();
	}
	bool Write(std::ostream& out) const {
		ASSERT(!tracks.empty());
		const int r(c.z),g(c.y),b(c.x);
		out << ID+1 << _T(" ")
			<< p.x << _T(" ") << p.y << _T(" ") << p.z << _T(" ")
			<< r << _T(" ") << g << _T(" ") << b << _T(" ")
			<< e << _T(" ");
		for (const Track& track: tracks) {
			out << track.idImage+1 << _T(" ") << track.idProj+1 << _T(" ");
			if (out.fail())
				return false;
		}
		out << std::endl;
		return !out.fail();
	}
};
typedef std::vector<Point> Points;
} // namespace COLMAP

typedef Eigen::Matrix<double,3,3,Eigen::RowMajor> EMat33d;
typedef Eigen::Matrix<double,3,1> EVec3d;

bool ImportScene(const String& strFolder, Interface& scene)
{
	// read camera list
	typedef std::unordered_map<uint32_t,uint32_t> CamerasMap;
	CamerasMap mapCameras;
	{
		const String filenameCameras(strFolder+COLMAP_CAMERAS);
		LOG_OUT() << "Reading cameras: " << filenameCameras << std::endl;
		std::ifstream file(filenameCameras.c_str());
		if (!file.good()) {
			VERBOSE("error: unable to open file '%s'", filenameCameras.c_str());
			return false;
		}
		typedef std::unordered_set<COLMAP::Camera, COLMAP::Camera::CameraHash, COLMAP::Camera::CameraEqualTo> CamerasSet;
		CamerasSet setCameras;
		{
		COLMAP::Camera camera;
		while (file.good() && camera.Read(file))
			setCameras.emplace(camera);
		}
		mapCameras.reserve(setCameras.size());
		for (const COLMAP::Camera& colmapCamera: setCameras) {
			mapCameras[colmapCamera.ID] = (uint32_t)scene.platforms.size();
			Interface::Platform platform;
			platform.name = String::FormatString(_T("platform%03u"), colmapCamera.ID); // only one camera per platform supported
			Interface::Platform::Camera camera;
			camera.name = colmapCamera.model;
			camera.K = Interface::Mat33d::eye();
			camera.K(0,0) = colmapCamera.params[0];
			camera.K(1,1) = colmapCamera.params[1];
			camera.K(0,2) = colmapCamera.params[2];
			camera.K(1,2) = colmapCamera.params[3];
			camera.R = Interface::Mat33d::eye();
			camera.C = Interface::Pos3d(0,0,0);
			if (OPT::bNormalizeIntrinsics) {
				// normalize camera intrinsics
				const double fScale(1.0/Camera::GetNormalizationScale(colmapCamera.width, colmapCamera.height));
				camera.K(0,0) *= fScale;
				camera.K(1,1) *= fScale;
				camera.K(0,2) *= fScale;
				camera.K(1,2) *= fScale;
			} else {
				camera.width = colmapCamera.width;
				camera.height = colmapCamera.height;
			}
			platform.cameras.push_back(camera);
			scene.platforms.push_back(platform);
		}
	}

	// read images list
	typedef std::map<COLMAP::Image, uint32_t> ImagesMap;
	ImagesMap mapImages;
	{
		const String filenameImages(strFolder+COLMAP_IMAGES);
		LOG_OUT() << "Reading images: " << filenameImages << std::endl;
		std::ifstream file(filenameImages.c_str());
		if (!file.good()) {
			VERBOSE("error: unable to open file '%s'", filenameImages.c_str());
			return false;
		}
		{
		COLMAP::Image image;
		while (file.good() && image.Read(file))
			mapImages[image];
		}
		for (ImagesMap::value_type& it: mapImages) {
			it.second = (uint32_t)scene.images.size();
			Interface::Platform::Pose pose;
			Eigen::Map<EMat33d>(pose.R.val) = it.first.q.toRotationMatrix();
			EnsureRotationMatrix((Matrix3x3d&)pose.R);
			Eigen::Map<EVec3d>(&pose.C.x) = -(it.first.q.inverse() * it.first.t);
			Interface::Image image;
			image.name = OPT::strImageFolder+it.first.name;
			image.platformID = mapCameras.at(it.first.idCamera);
			image.cameraID = 0;
			Interface::Platform& platform = scene.platforms[image.platformID];
			image.poseID = (uint32_t)platform.poses.size();
			platform.poses.push_back(pose);
			scene.images.push_back(image);
		}
	}

	// read points list
	{
		const String filenamePoints(strFolder+COLMAP_POINTS);
		LOG_OUT() << "Reading points: " << filenamePoints << std::endl;
		std::ifstream file(filenamePoints.c_str());
		if (!file.good()) {
			VERBOSE("error: unable to open file '%s'", filenamePoints.c_str());
			return false;
		}
		COLMAP::Point point;
		while (file.good() && point.Read(file)) {
			Interface::Vertex vertex;
			vertex.X = point.p;
			for (const COLMAP::Point::Track& track: point.tracks) {
				Interface::Vertex::View view;
				view.imageID = mapImages[COLMAP::Image(track.idImage)];
				view.confidence = 0;
				vertex.views.push_back(view);
			}
			std::sort(vertex.views.begin(), vertex.views.end(),
				[](const Interface::Vertex::View& view0, const Interface::Vertex::View& view1) { return view0.imageID < view1.imageID; });
			scene.vertices.push_back(vertex);
			Interface::Color color;
			color.c = point.c;
			scene.verticesColor.push_back(color);
		}
	}
	return true;
}


bool ExportScene(const String& strFolder, const Interface& scene)
{
	Util::ensureFolder(strFolder+COLMAP_SPARSE_FOLDER);

	// write camera list
	CLISTDEF0IDX(KMatrix,uint32_t) Ks;
	{
		const String filenameCameras(strFolder+COLMAP_CAMERAS);
		LOG_OUT() << "Writing cameras: " << filenameCameras << std::endl;
		std::ofstream file(filenameCameras.c_str());
		if (!file.good()) {
			VERBOSE("error: unable to open file '%s'", filenameCameras.c_str());
			return false;
		}
		file << _T("# Camera list with one line of data per camera:") << std::endl;
		file << _T("#   CAMERA_ID, MODEL, WIDTH, HEIGHT, PARAMS[]") << std::endl;
		COLMAP::Camera cam;
		cam.model = _T("PINHOLE");
		cam.params.resize(4);
		for (uint32_t ID=0; ID<(uint32_t)scene.platforms.size(); ++ID) {
			const Interface::Platform& platform = scene.platforms[ID];
			ASSERT(platform.cameras.size() == 1); // only one camera per platform supported
			const Interface::Platform::Camera& camera = platform.cameras[0];
			cam.ID = ID;
			cam.params[0] = camera.K(0,0);
			cam.params[1] = camera.K(1,1);
			cam.params[2] = camera.K(0,2);
			cam.params[3] = camera.K(1,2);
			if (camera.width == 0 || camera.height == 0) {
				// find one image using this camera
				const Interface::Image* pImage(NULL);
				for (uint32_t i=0; i<(uint32_t)scene.images.size(); ++i) {
					const Interface::Image& image = scene.images[i];
					if (image.platformID == ID && image.cameraID == 0 && image.poseID != NO_ID) {
						pImage = &image;
						break;
					}
				}
				if (pImage == NULL) {
					LOG("error: no image using camera %u of platform %u", 0, ID);
					continue;
				}
				IMAGEPTR ptrImage(Image::ReadImageHeader(MAKE_PATH_SAFE(pImage->name.c_str())));
				if (ptrImage == NULL)
					return false;
				cam.width = ptrImage->GetWidth();
				cam.height = ptrImage->GetHeight();
				// denormalize camera intrinsics
				const double fScale(MVS::Camera::GetNormalizationScale(cam.width, cam.height));
				cam.params[0] *= fScale;
				cam.params[1] *= fScale;
				cam.params[2] *= fScale;
				cam.params[3] *= fScale;
			} else {
				cam.width = camera.width;
				cam.height = camera.height;
			}
			if (!cam.Write(file))
				return false;
			KMatrix& K = Ks.AddEmpty();
			K = KMatrix::IDENTITY;
			K(0,0) = cam.params[0];
			K(1,1) = cam.params[1];
			K(0,2) = cam.params[2];
			K(1,2) = cam.params[3];
		}
	}

	// create images list
	COLMAP::Images images;
	CameraArr cameras;
	{
		images.resize(scene.images.size());
		cameras.resize(scene.images.size());
		for (uint32_t ID=0; ID<(uint32_t)scene.images.size(); ++ID) {
			const Interface::Image& image = scene.images[ID];
			if (image.poseID == NO_ID)
				continue;
			const Interface::Platform& platform = scene.platforms[image.platformID];
			const Interface::Platform::Pose& pose = platform.poses[image.poseID];
			ASSERT(image.cameraID == 0);
			COLMAP::Image& img = images[ID];
			img.ID = ID;
			img.q = Eigen::Quaterniond(Eigen::Map<const EMat33d>(pose.R.val));
			img.t = -(img.q * Eigen::Map<const EVec3d>(&pose.C.x));
			img.idCamera = image.platformID;
			img.name = MAKE_PATH_REL(OPT::strImageFolder, image.name);
			Camera& camera = cameras[ID];
			camera.K = Ks[image.platformID];
			camera.R = pose.R;
			camera.C = pose.C;
			camera.ComposeP();
		}
	}

	// write points list
	{
		const String filenamePoints(strFolder+COLMAP_POINTS);
		LOG_OUT() << "Writing points: " << filenamePoints << std::endl;
		std::ofstream file(filenamePoints.c_str());
		if (!file.good()) {
			VERBOSE("error: unable to open file '%s'", filenamePoints.c_str());
			return false;
		}
		file << _T("# 3D point list with one line of data per point:") << std::endl;
		file << _T("#   POINT3D_ID, X, Y, Z, R, G, B, ERROR, TRACK[] as (IMAGE_ID, POINT2D_IDX)") << std::endl;
		for (uint32_t ID=0; ID<(uint32_t)scene.vertices.size(); ++ID) {
			const Interface::Vertex& vertex = scene.vertices[ID];
			COLMAP::Point point;
			point.ID = ID;
			point.p = vertex.X;
			for (const Interface::Vertex::View& view: vertex.views) {
				COLMAP::Image& img = images[view.imageID];
				COLMAP::Point::Track track;
				track.idImage = view.imageID;
				track.idProj = (uint32_t)img.projs.size();
				point.tracks.push_back(track);
				COLMAP::Image::Proj proj;
				proj.idPoint = ID;
				const Point3 X(vertex.X);
				ProjectVertex_3x4_3_2(cameras[view.imageID].P.val, X.ptr(), proj.p.data());
				img.projs.push_back(proj);
			}
			point.c = scene.verticesColor.empty() ? Interface::Col3(255,255,255) : scene.verticesColor[ID].c;
			point.e = 0;
			if (!point.Write(file))
				return false;
		}
	}

	// write images list
	{
		const String filenameImages(strFolder+COLMAP_IMAGES);
		LOG_OUT() << "Writing images: " << filenameImages << std::endl;
		std::ofstream file(filenameImages.c_str());
		if (!file.good()) {
			VERBOSE("error: unable to open file '%s'", filenameImages.c_str());
			return false;
		}
		file << _T("# Image list with two lines of data per image:") << std::endl;
		file << _T("#   IMAGE_ID, QW, QX, QY, QZ, TX, TY, TZ, CAMERA_ID, NAME") << std::endl;
		file << _T("#   POINTS2D[] as (X, Y, POINT3D_ID)") << std::endl;
		for (const COLMAP::Image& img: images) {
			if (!img.projs.empty() && !img.Write(file))
				return false;
		}
	}

	Util::ensureFolder(strFolder+COLMAP_STEREO_FOLDER);

	// write fusion list
	{
		const String filenameFusion(strFolder+COLMAP_FUSION);
		LOG_OUT() << "Writing fusion configuration: " << filenameFusion << std::endl;
		std::ofstream file(filenameFusion.c_str());
		if (!file.good()) {
			VERBOSE("error: unable to open file '%s'", filenameFusion.c_str());
			return false;
		}
		for (const COLMAP::Image& img: images) {
			if (img.projs.empty())
				continue;
			file << img.name << std::endl;
			if (file.fail())
				return false;
		}
	}

	// write patch-match list
	{
		const String filenameFusion(strFolder+COLMAP_PATCHMATCH);
		LOG_OUT() << "Writing patch-match configuration: " << filenameFusion << std::endl;
		std::ofstream file(filenameFusion.c_str());
		if (!file.good()) {
			VERBOSE("error: unable to open file '%s'", filenameFusion.c_str());
			return false;
		}
		for (const COLMAP::Image& img: images) {
			if (img.projs.empty())
				continue;
			file << img.name << std::endl;
			if (file.fail())
				return false;
			file << _T("__auto__, 20") << std::endl;
			if (file.fail())
				return false;
		}
	}

	Util::ensureFolder(strFolder+COLMAP_STEREO_CONSISTENCYGRAPHS_FOLDER);
	Util::ensureFolder(strFolder+COLMAP_STEREO_DEPTHMAPS_FOLDER);
	Util::ensureFolder(strFolder+COLMAP_STEREO_NORMALMAPS_FOLDER);
	return true;
}

int main(int argc, LPCTSTR* argv)
{
	#ifdef _DEBUGINFO
	// set _crtBreakAlloc index to stop in <dbgheap.c> at allocation
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);// | _CRTDBG_CHECK_ALWAYS_DF);
	#endif

	if (!Initialize(argc, argv))
		return EXIT_FAILURE;

	TD_TIMER_START();

	if (OPT::b3Dnovator2COLMAP) {
		// read MVS input data
		Interface scene;
		if (!ARCHIVE::SerializeLoad(scene, MAKE_PATH_SAFE(OPT::strInputFileName)))
			return EXIT_FAILURE;
		// write COLMAP input data
		ExportScene(MAKE_PATH_SAFE(OPT::strOutputFileName), scene);
		VERBOSE("Input data exported: %u images & %u vertices (%s)", scene.images.size(), scene.vertices.size(), TD_TIMER_GET_FMT().c_str());
	} else {
		// read COLMAP input data
		Interface scene;
		if (!ImportScene(MAKE_PATH_SAFE(OPT::strInputFileName), scene))
			return EXIT_FAILURE;
		// write MVS input data
		Util::ensureFolder(Util::getFullPath(MAKE_PATH_FULL(WORKING_FOLDER_FULL, OPT::strOutputFileName)));
		if (!ARCHIVE::SerializeSave(scene, MAKE_PATH_SAFE(OPT::strOutputFileName), (uint32_t)OPT::bNormalizeIntrinsics?0:1))
			return EXIT_FAILURE;
		VERBOSE("Exported data: %u images & %u vertices (%s)", scene.images.size(), scene.vertices.size(), TD_TIMER_GET_FMT().c_str());
	}

	Finalize();
	return EXIT_SUCCESS;
}
/*----------------------------------------------------------------*/
