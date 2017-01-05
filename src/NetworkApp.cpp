#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"

using namespace ci;
using namespace ci::app;
using namespace std;

class NetworkApp : public App {
  public:
	void setup() override;
	void mouseDown( MouseEvent event ) override;
	void update() override;
	void draw() override;
};

void NetworkApp::setup()
{
}

void NetworkApp::mouseDown( MouseEvent event )
{
}

void NetworkApp::update()
{
}

void NetworkApp::draw()
{
	gl::clear( Color( 0, 0, 0 ) ); 
}

CINDER_APP( NetworkApp, RendererGl )
