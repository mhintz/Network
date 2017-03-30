#include <vector>
#include <unordered_set>
#include <utility>
#include <algorithm>
#include <numeric>

#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/Camera.h"
#include "cinder/CameraUi.h"
#include "cinder/Rand.h"

#include "CoreMath.h"
#include "FboCubeMapLayered.h"

using namespace ci;
using namespace ci::app;

class NetworkNode {
public:
	bool mInfected = false;
	uint mId;
	vec3 mPos;
	std::unordered_set<uint> mLinks;

	NetworkNode(uint id, vec3 pos) : mId(id), mPos(pos) {}
};

typedef std::shared_ptr<NetworkNode> NetworkNodeRef;

class NetworkApp : public App {
public:
	static void prepSettings(Settings * settings);

	void setup() override;
	void mouseDown( MouseEvent event ) override;
	void keyDown(KeyEvent evt) override;
	void update() override;
	void draw() override;

	CameraPersp mCamera;
	CameraUi mCameraUi;

	std::vector<NetworkNode> mNetworkNodes;
	std::vector<std::pair<uint, uint>> mNetworkLinks;

	gl::VboMeshRef mNodesMesh;
	gl::VboMeshRef mLinksMesh;

	gl::GlslProgRef mRenderCubeMap;

	FboCubeMapLayeredRef mRenderFbo;
	mat4 mFaceCams[6];
	gl::UboRef mMatrixBuffer;
	gl::GlslProgRef mRenderLinesToCubeMap;
	gl::GlslProgRef mRenderPointsToCubeMap;
};

int const numNetworkNodes = 1024;
int const cubeMapSide = 1600;
int const cubeMapBindPoint = 0;

void NetworkApp::prepSettings(Settings * settings) {
	settings->setFullScreen();
	settings->setHighDensityDisplayEnabled();
}

void NetworkApp::setup()
{
	// Set up the external user camera
	mCamera.lookAt(vec3(0, 0, 4), vec3(0), vec3(0, 1, 0));
	mCameraUi = CameraUi(& mCamera, getWindow());

	// Set up the 360 degree cube map camera
	auto cubeMapFormat = gl::TextureCubeMap::Format()
		.magFilter(GL_LINEAR)
		.minFilter(GL_LINEAR)
		.internalFormat(GL_RGB8);

	auto cubeMapFboFmt = FboCubeMapLayered::Format().colorFormat(cubeMapFormat);
	mRenderFbo = FboCubeMapLayered::create(cubeMapSide, cubeMapSide, cubeMapFboFmt);

	static vec3 const viewDirs[] = { vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1) };
	for (int idx = 0; idx < 6; idx++) {
		CameraPersp faceCam(cubeMapSide, cubeMapSide, 90.0f, 0.5, 5.0);
		faceCam.lookAt(vec3(0), viewDirs[idx]);
		mat4 result;
		if (idx != 2 && idx != 3) {
			result *= glm::rotate((float) M_PI, vec3(0, 0, 1));
		}
		result *= faceCam.getProjectionMatrix() * faceCam.getViewMatrix();
		mFaceCams[idx] = result;
	}

	int const matrixBindingPoint = 1;

	mMatrixBuffer = gl::Ubo::create(sizeof(mat4) * 6, mFaceCams);
	mMatrixBuffer->bindBufferBase(matrixBindingPoint);

	mRenderLinesToCubeMap = gl::GlslProg::create(loadAsset("renderIntoCubeMap_v.glsl"), loadAsset("renderIntoCubeMap_f.glsl"), loadAsset("renderIntoCubeMap_lines_g.glsl"));
	mRenderLinesToCubeMap->uniformBlock("uMatrices", matrixBindingPoint);

	mRenderPointsToCubeMap = gl::GlslProg::create(loadAsset("renderIntoCubeMap_v.glsl"), loadAsset("renderIntoCubeMap_f.glsl"), loadAsset("renderIntoCubeMap_points_g.glsl"));
	mRenderPointsToCubeMap->uniformBlock("uMatrices", matrixBindingPoint);

	// Set up the simulation data
	for (int idx = 0; idx < numNetworkNodes; idx++) {
		mNetworkNodes.push_back(NetworkNode(idx, randVec3()));
		if (randFloat() < 0.1) { mNetworkNodes[idx].mInfected = true; }
	}

	std::vector<NetworkNode *> nodePointers;
	for (auto & node : mNetworkNodes) {
		nodePointers.push_back(& node);
	}

	for (auto & node : mNetworkNodes) {
		// Sort according to distance
		std::sort(nodePointers.begin(), nodePointers.end(), [& node] (NetworkNode * p1, NetworkNode * p2) {
			float d1 = distance(node.mPos, p1->mPos);
			float d2 = distance(node.mPos, p2->mPos);
			return d1 < d2;
		});
		// Take the nearest six (knowing the the first one will be the node itself)
		for (int i = 1; i <= 6; i++) {
			node.mLinks.insert(nodePointers[i]->mId);
			nodePointers[i]->mLinks.insert(node.mId);
			mNetworkLinks.push_back(std::make_pair(nodePointers[i]->mId, node.mId));
		}
	}

	// Set up OpenGL data structures on the GPU
	size_t numNodes = mNetworkNodes.size();

	std::vector<vec3> nodePositions(numNodes);
	std::vector<vec3> nodeColors(numNodes);
	for (int idx = 0; idx < numNodes; idx++) {
		nodePositions[idx] = mNetworkNodes[idx].mPos;
		nodeColors[idx] = mNetworkNodes[idx].mInfected ? vec3(1, 0, 0) : vec3(0, 0, 1);
	}

	auto nodesBuf = gl::Vbo::create(GL_ARRAY_BUFFER, nodePositions);
	auto nodesFmt = geom::BufferLayout({ geom::AttribInfo(geom::POSITION, 3, 0, 0) });
	auto nodeColorsBuf = gl::Vbo::create(GL_ARRAY_BUFFER, nodeColors, GL_DYNAMIC_DRAW);
	auto nodeColorsFmt = geom::BufferLayout({ geom::AttribInfo(geom::COLOR, 3, 0, 0) });
	mNodesMesh = gl::VboMesh::create(numNodes, GL_POINTS, { { nodesFmt, nodesBuf }, { nodeColorsFmt, nodeColorsBuf } });

	size_t numLinks = mNetworkLinks.size();

	std::vector<vec3> linkPositions(2 * numLinks);
	std::vector<vec3> linkColors(2 * numLinks);
	for (int idx = 0; idx < numLinks; idx++) {
		uint id1 = mNetworkLinks[idx].first;
		uint id2 = mNetworkLinks[idx].second;
		linkPositions[2 * idx] = mNetworkNodes[id1].mPos;
		linkPositions[2 * idx + 1] = mNetworkNodes[id2].mPos;
		linkColors[2 * idx] = mNetworkNodes[id1].mInfected ? vec3(1, 0, 0) : vec3(0, 0, 1);
		linkColors[2 * idx + 1] = mNetworkNodes[id2].mInfected ? vec3(1, 0, 0) : vec3(0, 0, 1);
	}

	auto linksBuf = gl::Vbo::create(GL_ARRAY_BUFFER, linkPositions);
	auto linksFmt = geom::BufferLayout({ geom::AttribInfo(geom::POSITION, 3, 0, 0) });
	auto linkColorsBuf = gl::Vbo::create(GL_ARRAY_BUFFER, linkColors, GL_DYNAMIC_DRAW);
	auto linkColorsFmt = geom::BufferLayout({ geom::AttribInfo(geom::COLOR, 3, 0, 0) });
	mLinksMesh = gl::VboMesh::create(2 * numLinks, GL_LINES, { { linksFmt, linksBuf }, { linkColorsFmt, linkColorsBuf } });

	// Set up for rendering the contents of the 360 degree cube map camera
	mRenderCubeMap = gl::GlslProg::create(loadAsset("renderCubeMap_v.glsl"), loadAsset("renderCubeMap_f.glsl"));
	mRenderCubeMap->uniform("uCubeMap", 0);

	// OpenGL state stuff
	gl::enableDepth();
	gl::pointSize(5.0);
}

void NetworkApp::mouseDown( MouseEvent event )
{
}

void NetworkApp::keyDown(KeyEvent evt) {
	if (evt.getCode() == KeyEvent::KEY_ESCAPE) {
		quit();
	}
}

void NetworkApp::update()
{
	std::vector<bool> willBeInfected(mNetworkNodes.size(), false);

	for (int idx = 0; idx < mNetworkNodes.size(); idx++) {
		auto & node = mNetworkNodes[idx];
		if (node.mInfected) {
			if (randFloat() < 0.04) { willBeInfected[node.mId] = false; } else { willBeInfected[node.mId] = true; }
			for (uint otherId : node.mLinks) {
				if (randFloat() < 0.01) { willBeInfected[otherId] = true; }
			}
		}
	}

	uint numInfected = std::accumulate(willBeInfected.begin(), willBeInfected.end(), 0, [] (uint count, bool inf) { return count + (inf ? 1 : 0); });
	if (numInfected < 10) {
		for (auto & node : mNetworkNodes) {
			if (randFloat() < 10.f / numNetworkNodes) { node.mInfected = true; }
		}
	}

	for (int idx = 0; idx < mNetworkNodes.size(); idx++) {
		mNetworkNodes[idx].mInfected = willBeInfected[idx];
	}

	std::vector<vec3> nodeColors(mNetworkNodes.size());
	for (int idx = 0; idx < mNetworkNodes.size(); idx++) {
		nodeColors[idx] = mNetworkNodes[idx].mInfected ? vec3(1, 0, 0) : vec3(0, 0, 1);
	}

	mNodesMesh->findAttrib(geom::COLOR)->second->copyData(vectorByteSize(nodeColors), nodeColors.data());

	std::vector<vec3> linkColors(2 * mNetworkLinks.size());
	for (int idx = 0; idx < mNetworkLinks.size(); idx++) {
		uint id1 = mNetworkLinks[idx].first;
		uint id2 = mNetworkLinks[idx].second;
		linkColors[2 * idx] = mNetworkNodes[id1].mInfected ? vec3(1, 0, 0) : vec3(0, 0, 1);
		linkColors[2 * idx + 1] = mNetworkNodes[id2].mInfected ? vec3(1, 0, 0) : vec3(0, 0, 1);
	}

	mLinksMesh->findAttrib(geom::COLOR)->second->copyData(vectorByteSize(linkColors), linkColors.data());
}

void NetworkApp::draw()
{
	gl::clear(Color(0, 0, 0));

	{
		gl::ScopedMatrices scpMat;
		gl::setMatrices(mCamera);

		gl::draw(mNodesMesh);
		gl::draw(mLinksMesh);
	}

	{
		gl::ScopedViewport scpView(0, 0, mRenderFbo->getWidth(), mRenderFbo->getHeight());

		gl::ScopedMatrices scpMat;

		gl::ScopedFramebuffer scpFbo(GL_FRAMEBUFFER, mRenderFbo->getId());

		gl::clear(Color(0, 0, 0));

		{
			gl::ScopedGlslProg scpShader(mRenderLinesToCubeMap);

			gl::draw(mLinksMesh);
		}

		{
			gl::ScopedGlslProg scpShader(mRenderPointsToCubeMap);

			gl::draw(mNodesMesh);
		}
	}

	{
		gl::ScopedMatrices scpMat;
		gl::setMatrices(mCamera);

		gl::ScopedGlslProg scpShader(mRenderCubeMap);

		gl::ScopedTextureBind scpTex(mRenderFbo->getColorTex(), cubeMapBindPoint);
		glGenerateMipmap(mRenderFbo->getColorTex()->getTarget()); // Do this each frame?

		gl::draw(geom::Sphere().radius(0.5f).subdivisions(50));
		// gl::draw(geom::Sphere().radius(1.0f));
		// gl::draw(geom::Cube().size(1.0f, 1.0f, 1.0f));
	}

	// Debug zone
	{
		 gl::drawHorizontalCross(mRenderFbo->getColorTex(), Rectf(0, 0, 1200.0f, 600.0f));
	}
}

CINDER_APP( NetworkApp, RendererGl, &NetworkApp::prepSettings )
