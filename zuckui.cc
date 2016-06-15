// g++ -std=c++98 -O2 -s -static-libgcc -static-libstdc++ -o zuckui zuckui.cc -Wl,-Bstatic -lfltk -Wl,-Bdynamic -lcurl -lmpv -DUSE_MPV -lgdi32 -lcomctl32 -lole32 -luuid
// g++ -std=c++98 -O2 -s -static-libgcc -static-libstdc++ -o zuckui zuckui.cc -Wl,-Bstatic -lfltk -Wl,-Bdynamic -lcurl -L"C:\Program Files (x86)\VideoLAN\VLC" -lvlc -DUSE_VLC=1 -lgdi32 -lcomctl32 -lole32 -luuid

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <curl/curl.h>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Tile.H>
#include <FL/Fl_Box.H>
#include <FL/x.H>

#ifdef USE_MPV
	#define Video Fl_Mpv
	#include <mpv/client.h>
	#include "fl_mpv.h"
#elif USE_VLC
	#define Video Fl_Vlc
	#include <vlc/vlc.h>
	#include "fl_vlc.h"
#else
	#error "no video player set"
#endif

#define JSMN_STRICT
#define JSMN_PARENT_LINKS
#include "jsmn.cc"
#include "twitch.cc"

struct ZuckChat : public Fl_Group {
	const static int defaultWidth = 250;
	ZuckChat(int X, int Y, int W, int H, const char*L=0)
		: Fl_Group(X,Y,W,H,L) {

		Fl_Box *green = new Fl_Box(X, Y, W, H, "chat");
		green->box(FL_DOWN_BOX);
		green->color(fl_rgb_color(0x33, 0x33, 0x33));
		green->labelsize(36);
		green->align(FL_ALIGN_CLIP);
		end();
	}
};

struct VideoControls : public Fl_Group {
	const static int defaultHeight = 80;
	VideoControls(int X, int Y, int W, int H, const char*L=0)
		: Fl_Group(X,Y,W,H,L) {

		Fl_Box *green = new Fl_Box(X, Y, W, H, "controls");
		green->box(FL_DOWN_BOX);
		green->color(fl_rgb_color(0x66, 0x66, 0x66));
		green->labelsize(36);
		green->align(FL_ALIGN_CLIP);
		end();
	}
};

struct RelativeBox : public Fl_Box {
	int rx, ry, rw, rh;
	Fl_Widget *root;
	RelativeBox(Fl_Widget *root, int X, int Y, int W, int H, const char*L=0)
		: Fl_Box(0,0,0,0,L) {
		this->root = root;
		this->rx = X;
		this->ry = Y;
		this->rw = W;
		this->rh = H;
		resize(0,0,0,0);
	}
	void resize(int X, int Y, int W, int H) {
		Fl_Box::resize(
			root->x()+rx,
			root->y()+ry,
			root->w()+rw,
			root->h()+rh
		);
	}
};

// TODO: combine HorizontalSplitTile and VerticalSplitTile (?)
struct HorizontalSplitTile : public Fl_Tile {
	Fl_Widget *_main;
	Fl_Widget *_side;
	int sideDefault;
	HorizontalSplitTile(int sideDefault, int X, int Y, int W, int H, const char*L=0)
		: Fl_Tile(X,Y,W,H,L) {
		_main = NULL;
		_side = NULL;
		this->sideDefault = sideDefault;
	}
	void main(Fl_Widget *w) {
		this->_main = w;
	}
	void side(Fl_Widget *w) {
		this->_side = w;
	}
	void resize(int X, int Y, int W, int H) {
		float orw = (float)W/w();
		Fl_Widget *r = resizable();
		Fl_Widget::resize(X,Y,W,H);
		r->resize(X, Y, W, H);
		int ww = (int)(_main->w()*orw + 0.5);
		int rr = r->x()+r->w();
		if(ww > rr) {
			ww = rr;
		}
		resize_children(X, Y, ww, H);
	}
	void resize_children(int X, int Y, int W, int H) {
		_main->resize(X, Y, W, H);
		_side->resize(X+W, Y, w()-W, H);
	}
	int handle(int event) {
		if(event == FL_RELEASE &&
			Fl::event_button() == FL_LEFT_MOUSE &&
			Fl::event_clicks() != 0) {
			resize_children(x(), y(), w()-sideDefault, h());
			redraw();
			return Fl_Group::handle(event);
		}
		return Fl_Tile::handle(event);
	}
};

struct VerticalSplitTile : public Fl_Tile {
	Fl_Widget *_main;
	Fl_Widget *_side;
	int sideDefault;
	VerticalSplitTile(int sideDefault, int X, int Y, int W, int H, const char*L=0)
		: Fl_Tile(X,Y,W,H,L) {
		_main = NULL;
		_side = NULL;
		this->sideDefault = sideDefault;
	}
	void main(Fl_Widget *w) {
		this->_main = w;
	}
	void side(Fl_Widget *w) {
		this->_side = w;
	}
	void resize(int X, int Y, int W, int H) {
		float orh = (float)H/h();
		Fl_Widget *r = resizable();
		Fl_Widget::resize(X,Y,W,H);
		r->resize(X, Y, W, H);
		int ww = (int)(_main->h()*orh + 0.5);
		int rr = r->y()+r->h();
		if(ww > rr) {
			ww = rr;
		}
		resize_children(X, Y, W, ww);
	}
	void resize_children(int X, int Y, int W, int H) {
		_main->resize(X, Y, W, H);
		_side->resize(X, Y+H, W, h()-H);
	}
	int handle(int event) {
		if(event == FL_RELEASE &&
			Fl::event_button() == FL_LEFT_MOUSE &&
			Fl::event_clicks() != 0) {
			resize_children(x(), y(), w(), h()-sideDefault);
			redraw();
			return Fl_Group::handle(event);
		}
		return Fl_Tile::handle(event);
	}
};

struct ZuckWindow : public Fl_Window {
	Video *video;
	ZuckChat *chat;
	VideoControls *controls;
	const static int vidx = 1024;
	const static int vidy = 768;
	ZuckWindow() : Fl_Window(
		vidx+ZuckChat::defaultWidth,
		vidy+VideoControls::defaultHeight,
		"Zuck"
	) {
		srand(time(NULL));
		curl_global_init(CURL_GLOBAL_DEFAULT);
		HorizontalSplitTile *maintile = new HorizontalSplitTile(
			ZuckChat::defaultWidth, 0, 0, w(), h()
		);
			VerticalSplitTile *videotile = new VerticalSplitTile(
				VideoControls::defaultHeight, 0, 0, vidx, h()
			);
				video = new Video(0, 0, vidx, vidy);
				controls = new VideoControls(0, vidy, vidx, videotile->h()-vidy);
				RelativeBox *videobox = new RelativeBox(videotile, 0, 0, 0, -5);
				videobox->hide();
				videotile->resizable(videobox);
				videotile->main(video);
				videotile->side(controls);
			videotile->end();
			chat = new ZuckChat(vidx, 0, maintile->w()-vidx, h());
			RelativeBox *mainbox = new RelativeBox(maintile, 0, 0, -5, 0);
			mainbox->hide();
			maintile->resizable(mainbox);
			maintile->main(videotile);
			maintile->side(chat);
		maintile->end();
		end();
	}
	~ZuckWindow() {
		curl_global_cleanup();
	}
	void loadStream(String name, Quality q) {
		Stream stream = {0};
		if (get_live_stream(&stream, name, q)) {
			#ifdef USE_MPV
			video->loadFile(stream.url.data);
			#elif USE_VLC
			video->loadUrl(stream.url.data);
			#endif
			free(stream.url.data);
		}
	}
	void show() {
		Fl_Window::show();
		this->video->initializeHandle();
		#ifdef _WIN32
		position((Fl::w()-w())/2,(Fl::h()-h())/2);
		#endif
	}
};

int main(int argc, char **argv) {
	ZuckWindow win;
	win.resizable(win);
	win.show();
	// win.fullscreen();
	win.loadStream(STRING("food"), Quality_Medium);
	return Fl::run();
}
