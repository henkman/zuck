
// include <FL/Fl.H>
// include <FL/Fl_Window.H>
// include <FL/x.H>
// include <mpv/client.h>

struct Fl_Mpv : public Fl_Window {
	mpv_handle *mpv;
	Fl_Mpv(int X, int Y, int W, int H, const char*L=0)
		: Fl_Window(X,Y,W,H,L)
	{
		mpv = mpv_create();
		mpv_set_option_string(mpv, "keep-open", "always");
		mpv_initialize(mpv);
		end();
	}
	~Fl_Mpv() {
		mpv_terminate_destroy(mpv);
	}
	void initializeHandle() {
		#ifdef _WIN32
			mpv_set_option_string(mpv, "vo", "direct3d");
		#elif __APPLE__
			mpv_set_option_string(mpv, "vo", "opengl");
		#else
			mpv_set_option_string(mpv, "vo", "opengl");
		#endif
		int64_t wid = (int64_t)fl_xid(this);
		mpv_set_option(mpv, "wid", MPV_FORMAT_INT64, &wid);
	}
	void loadFile(const char* url) {
		const char *cmd[] = {"loadfile", url, NULL};
		mpv_command(mpv, cmd);
	}
};