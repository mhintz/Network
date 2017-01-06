#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/Camera.h"
#include "cinder/CameraUi.h"
#include "cinder/Rand.h"

#include "FboCubeMapLayered.h"

using namespace ci;
using namespace ci::app;
using namespace std;

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

	vector<vec3> mNetworkNodes;
	vector<vec3> mNetworkLinks;

	gl::VboMeshRef mNodesMesh;
	gl::VboMeshRef mLinksMesh;

	gl::GlslProgRef mRenderCubeMap;

	FboCubeMapLayeredRef mRenderFbo;
	mat4 mFaceCams[6];
	gl::UboRef mMatrixBuffer;
	gl::GlslProgRef mRenderLinesToCubeMap;
	gl::GlslProgRef mRenderPointsToCubeMap;
};

int const numNetworkNodes = 600;
int const cubeMapSide = 1600;
int const cubeMapBindPoint = 0;

void NetworkApp::prepSettings(Settings * settings) {
	settings->setFullScreen();
	settings->setHighDensityDisplayEnabled();
}

void NetworkApp::setup()
{
	mCamera.lookAt(vec3(0, 0, 4), vec3(0), vec3(0, 1, 0));
	mCameraUi = CameraUi(& mCamera, getWindow());

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

	mMatrixBuffer = gl::Ubo::create(sizeof(mat4) * 6, mFaceCams);
	mMatrixBuffer->bindBufferBase(1);

	mRenderLinesToCubeMap = gl::GlslProg::create(loadAsset("renderIntoCubeMap_v.glsl"), loadAsset("renderIntoCubeMap_f.glsl"), loadAsset("renderIntoCubeMap_lines_g.glsl"));
	mRenderLinesToCubeMap->uniformBlock("uMatrices", 1);

	mRenderPointsToCubeMap = gl::GlslProg::create(loadAsset("renderIntoCubeMap_v.glsl"), loadAsset("renderIntoCubeMap_f.glsl"), loadAsset("renderIntoCubeMap_points_g.glsl"));
	mRenderPointsToCubeMap->uniformBlock("uMatrices", 1);

	for (int idx = 0; idx < numNetworkNodes; idx++) {
		mNetworkNodes.push_back(randVec3());
	}

	for (vec3 & nodePos : mNetworkNodes) {
		for (vec3 & otherPos : mNetworkNodes) {
			float dist = distance(nodePos, otherPos);
			if (dist > 0 && dist < 0.2) {
				mNetworkLinks.push_back(nodePos);
				mNetworkLinks.push_back(otherPos);
			}
		}
	}

	auto nodesBuf = gl::Vbo::create(GL_ARRAY_BUFFER, mNetworkNodes);
	auto nodesFmt = geom::BufferLayout({ geom::AttribInfo(geom::POSITION, 3, 0, 0) });
	mNodesMesh = gl::VboMesh::create(mNetworkNodes.size(), GL_POINTS, { { nodesFmt, nodesBuf } });

	auto linksBuf = gl::Vbo::create(GL_ARRAY_BUFFER, mNetworkLinks);
	auto linksFmt = geom::BufferLayout({ geom::AttribInfo(geom::POSITION, 3, 0, 0) });
	mLinksMesh = gl::VboMesh::create(mNetworkLinks.size(), GL_LINES, { { linksFmt, linksBuf } });

	mRenderCubeMap = gl::GlslProg::create(loadAsset("renderCubeMap_v.glsl"), loadAsset("renderCubeMap_f.glsl"));
	mRenderCubeMap->uniform("uCubeMap", 0);

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
}

void NetworkApp::draw()
{
	gl::clear(Color(0, 0, 0));

	{
		gl::ScopedMatrices scpMat;
		gl::setMatrices(mCamera);

		gl::ScopedColor scpColor(Color(1, 1, 1));

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

			gl::ScopedColor scpColor(Color(1, 1, 1));

			gl::draw(mLinksMesh);
		}

		{
			gl::ScopedGlslProg scpShader(mRenderPointsToCubeMap);

			gl::ScopedColor scpColor(Color(1, 1, 1));

			gl::draw(mNodesMesh);
		}
	}

	{
		gl::ScopedMatrices scpMat;
		gl::setMatrices(mCamera);

		gl::ScopedGlslProg scpShader(mRenderCubeMap);

		gl::ScopedTextureBind scpTex(mRenderFbo->getColorTex(), cubeMapBindPoint);
		glGenerateMipmap(mRenderFbo->getColorTex()->getTarget()); // Do this each frame?

		gl::draw(geom::Sphere().radius(0.5f));
		// gl::draw(geom::Sphere().radius(1.0f));
		// gl::draw(geom::Cube().size(1.0f, 1.0f, 1.0f));
	}

	// Debug zone
	{
		// gl::drawHorizontalCross(mRenderFbo->getColorTex(), Rectf(0, 0, 600.0f, 300.0f));
	}
}

CINDER_APP( NetworkApp, RendererGl, &NetworkApp::prepSettings )
