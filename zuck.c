#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include "json.c"

#define STRING(x) ((String){data:(char *)x, size:sizeof(x)-1})

typedef struct {
	char *data;
	size_t size;
} String;

typedef struct {
	char *data;
	size_t size, cap;
} Chunk;

typedef struct {
	String url;
	struct {
		unsigned x, y;
	} Resolution;
} Stream;

typedef enum {
	Quality_Source,
	Quality_High,
	Quality_Medium,
	Quality_Low,
	Quality_Mobile,
	Quality_AudioOnly
} Quality;

static String QualityString[] = {
	[Quality_Source]    = STRING("Source"),
	[Quality_High]      = STRING("High"),
	[Quality_Medium]    = STRING("Medium"),
	[Quality_Low]       = STRING("Low"),
	[Quality_Mobile]    = STRING("Mobile"),
	[Quality_AudioOnly] = STRING("Audio Only"),
};

static size_t
next_power_of_two(size_t v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}
 
static size_t
write_chunk_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t bytesize = size * nmemb;
	Chunk *out = (Chunk *)userp;
	size_t newsize = out->size+bytesize;
	if (newsize >= out->cap) {
		out->cap = next_power_of_two(newsize);
		out->data = (char *)realloc(out->data, sizeof(char)*out->cap);
		if (!out->data || out->cap < newsize) {
			abort();
		}
	}
	memcpy(&out->data[out->size], contents, bytesize);
	out->size = newsize;
	return bytesize;
}

static int
http_get(const char *url, Chunk *out)
{
	CURL *curl = curl_easy_init();
	if (!curl) {
		return 0;
	}
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_chunk_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)out);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	int ok = 1;
	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		ok = 0;
	}
	curl_easy_cleanup(curl);
	return ok;
}

static int
find_json_object_child(String *out, json_value *root, String key)
{
	for (unsigned i=0; i<root->u.object.length; i++) {
		json_object_entry *oe = &root->u.object.values[i];
		if (key.size == oe->name_length &&
			memcmp(key.data, oe->name, key.size) == 0 &&
			oe->value->type == json_string) {
			json_value *v = oe->value;
			out->data = v->u.string.ptr;
			out->size = v->u.string.length;
			return 1;
		}
	}
	return 0;
}

static unsigned
rand_limit(unsigned limit) {
    double divisor = ((double)RAND_MAX)/((double)limit);
    unsigned r;
    do { 
        r = rand()/divisor;
    } while (r == limit);
    return r;
}

static int
get_token_and_signature(String *sig, String *token, String channel)
{
	int ok = 1;
	sig->data = token->data = NULL;
	Chunk chunk = {
		.cap = 512,
		.size = 0
	};
	chunk.data = (char *)malloc(sizeof(char)*chunk.cap);
	char url[256];
	snprintf(
		url, sizeof(url),
		"http://api.twitch.tv/api/channels/%.*s/access_token",
		channel.size, channel.data
	);
	if (!http_get(url, &chunk)) {
		ok = 0;
		goto clean_chunk;	
	}
	{
		String s, t;
		json_value *root = json_parse(chunk.data, chunk.size);
		if (!root || root->type != json_object) {
			ok = 0;
			goto clean_json;
		}
		if (!find_json_object_child(&s, root, STRING("sig"))) {
			ok = 0;
			goto clean_json;
		}
		if (!find_json_object_child(&t, root, STRING("token"))) {
			ok = 0;
			goto clean_json;
		}
		sig->size = s.size;
		sig->data = malloc(sizeof(char)*s.size);
		memcpy(sig->data, s.data, s.size);
		token->size = t.size;
		token->data = malloc(sizeof(char)*t.size);
		memcpy(token->data, t.data, t.size);
	clean_json:
		json_value_free(root);
	}
clean_chunk:
	free(chunk.data);
	return ok;
}

static int
find_stream_url(Stream *s, String m3u8, Quality q)
{
	String *tofind = &QualityString[q];
	unsigned o = 0;
	#define skip_line() {                   \
		for (;;) {                          \
			if (o >= m3u8.size) {           \
				goto end;                   \
			}                               \
			if (m3u8.data[o] == '\n') {     \
				o++;                        \
				break;                      \
			}                               \
			o++;                            \
		}                                   \
	}
	{
		String magic = STRING("#EXTM3U\n");
		if (magic.size >= m3u8.size ||
			memcmp(m3u8.data, magic.data, magic.size) != 0) {
			return 0;
		}
		o += magic.size;
	}
	for (;;) {
		{
			String media = STRING("#EXT-X-MEDIA");
			if (o+media.size >= m3u8.size) {
				goto end;
			}
			if (memcmp(&m3u8.data[o], media.data, media.size) != 0) {
				skip_line();
				continue;
			}
		}
		{
			String name_begin = STRING("NAME=\"");
			unsigned found;
			for (;;) {
				if (o+name_begin.size >= m3u8.size) {
					goto end;
				}
				if (m3u8.data[o] == '\n') {
					found = 0;
					break;
				}
				if (
					memcmp(&m3u8.data[o], name_begin.data, name_begin.size) == 0
				) {
					o += name_begin.size;
					found = 1;
					break;
				}
				o++;
			}
			if (!found) {
				continue;
			}
			unsigned b = o;
			for (;;) {
				if (o >= m3u8.size) {
					goto end;
				}
				if (m3u8.data[o] == '\n') {
					found = 0;
					break;
				}
				if (m3u8.data[o] == '"') {
					found = 1;
					break;
				}
				o++;
			}
			if (!found) {
				continue;
			}
			if (o-b != tofind->size ||
				memcmp(&m3u8.data[b], tofind->data, tofind->size)) {
				skip_line();
				continue;
			}
			skip_line();
		}
		{
			// TODO: resolution
			skip_line();
		}
		{
			String http = STRING("http://");
			if (o+http.size >= m3u8.size) {
				goto end;
			}
			if (memcmp(&m3u8.data[o], http.data, http.size) != 0) {
				goto end;
			}
			unsigned b = o;
			for (;;) {
				if (o >= m3u8.size) {
					goto end;
				}
				if (m3u8.data[o] == '\n') {
					break;
				}
				o++;
			}
			unsigned size = o-b;
			s->url.size = size;
			s->url.data = malloc(sizeof(char)*(size));
			memcpy(s->url.data, &m3u8.data[b], size);
			return 1;
		}
	}
end:
	return 0;
}

static int
get_live_stream(Stream *stream, String channel, Quality q)
{
	String sig, token;
	if (!get_token_and_signature(&sig, &token, channel)) {
		if (sig.data) {
			free(sig.data);
		}
		if (token.data) {
			free(token.data);
		}
		return 0;
	}
	int ok = 0;
	Chunk chunk = {
		.cap = 512,
		.size = 0
	};
	chunk.data = (char *)malloc(sizeof(char)*chunk.cap);
	{
		char url[512];
		snprintf(
			url, sizeof(url),
			"http://usher.twitch.tv/api/channel/hls/%.*s.m3u8"
			"?player=twitchweb&token=%.*s&sig=%.*s"
			"&allow_audio_only=true&allow_source=true&type=any&p=%u",
			channel.size, channel.data,
			token.size, token.data,
			sig.size, sig.data,
			rand_limit(10000000)
		);
		free(sig.data);
		free(token.data);
		if (!http_get(url, &chunk)) {
			goto end;	
		}
	}
	String m3u8 = (String){.data=chunk.data, .size=chunk.size};
	if (find_stream_url(stream, m3u8, q)) {
		ok = 1;
	}
end:
	free(chunk.data);
	return ok;
}

int main(int argc, char **argv)
{
	srand(time(NULL));
	curl_global_init(CURL_GLOBAL_DEFAULT);
	Stream stream = {0};
	if (get_live_stream(&stream, STRING("food"), Quality_Medium)) {
		printf("%.*s\n", stream.url.size, stream.url.data);
		free(stream.url.data);
	}
	curl_global_cleanup();
	
	return 0;
}

