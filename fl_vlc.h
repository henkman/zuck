
// include <FL/Fl.H>
// include <FL/Fl_Window.H>
// include <FL/x.H>
// include <vlc/vlc.h>

struct Fl_Vlc : public Fl_Window {
	libvlc_instance_t *vlc;
	libvlc_media_player_t *player;
	libvlc_event_manager_t *events;
	Fl_Vlc(int X, int Y, int W, int H, const char*L=0)
		: Fl_Window(X,Y,W,H,L)
	{
		vlc = libvlc_new(0, NULL);
		player = libvlc_media_player_new(vlc);
		events = libvlc_media_player_event_manager(player);
		end();
	}
	~Fl_Vlc() {
		libvlc_media_player_release(player);
		libvlc_release(vlc);
	}
	void initializeHandle() {
		#ifdef _WIN32
		libvlc_media_player_set_hwnd(player, fl_xid(this));
		#elif __APPLE__
		libvlc_media_player_set_nsobject (mp, fl_xid(this));
		#else
		libvlc_media_player_set_xwindow(player, fl_xid(this));
		#endif
	}
	void loadUrl(const char* url) {
		libvlc_media_t *media;
		media = libvlc_media_new_location(vlc, url);
		libvlc_media_player_set_media(player, media);
		libvlc_media_player_play(player);
		libvlc_media_release(media);
	}
	void loadFile(const char* file) {
		libvlc_media_t *media;
		media = libvlc_media_new_path(vlc, file);
		libvlc_media_player_set_media(player, media);
		libvlc_media_player_play(player);
		libvlc_media_release(media);
	}
};
