// include <stdlib.h>
// include <stdio.h>
// include <string.h>
// include <time.h>
// include <curl/curl.h>
// define JSMN_PARENT_LINKS
// include "jsmn.c"

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
	String url; // NUL-terminated
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
jsoneq(Chunk *json, jsmntok_t *tok, String s)
{
	return tok->type == JSMN_STRING &&
		(int) s.size == tok->end - tok->start &&
		memcmp(
			json->data + tok->start,
			s.data,
			tok->end - tok->start
		) == 0;
}

static void
string_unescape(String *t, String *s)
{
	unsigned o = 0;
	for (unsigned i=0; i<s->size; i++) {
		if (s->data[i] == '\\') {
			if (i < s->size) {
				t->data[o++] = s->data[i+1];
				i++;
				continue;
			}
		}
		t->data[o++] = s->data[i]; 
	}
	t->size = o;
}

static int
get_token_and_signature(String *sig, String *token, String channel, Chunk *chunk)
{
	sig->data = token->data = NULL;
	char url[256];
	int n = snprintf(
		url, sizeof(url),
		"http://api.twitch.tv/api/channels/%.*s/access_token",
		channel.size, channel.data
	);
	if (n < 0 || n >= sizeof(url) || !http_get(url, chunk)) {
		return 0;
	}
	{
		jsmn_parser p;
		jsmntok_t t[16];
		jsmn_init(&p);
		int r = jsmn_parse(&p, chunk->data, chunk->size, t,
			sizeof(t)/sizeof(t[0]));
		if (r < 1 || t[0].type != JSMN_OBJECT) {
			return 0;
		}
		for (unsigned i = 1; i < r; i++) {
			if (jsoneq(chunk, &t[i], STRING("sig"))) {
				sig->size = t[i+1].end - t[i+1].start;
				sig->data = chunk->data + t[i+1].start;
				i++;
			} else if (jsoneq(chunk, &t[i], STRING("token"))) {
				token->size = t[i+1].end-t[i+1].start;
				token->data = chunk->data + t[i+1].start;
				i++;
			}
		}
		if (!sig->data || !token->data) {
			return 0;
		}
	}
	return 1;
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
		{ // EXT-X-STREAM-INF
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
			s->url.data = (char*)malloc(sizeof(char)*(size+1));
			memcpy(s->url.data, &m3u8.data[b], size);
			s->url.data[s->url.size] = 0;
			return 1;
		}
	}
end:
	return 0;
}

static int
get_live_stream(Stream *stream, String channel, Quality q)
{
	Chunk chunk;
	chunk.cap = 512;
	chunk.size = 0;
	chunk.data = (char *)malloc(sizeof(char)*chunk.cap);
	String sig, token;
	if (!get_token_and_signature(&sig, &token, channel, &chunk)) {
		free(chunk.data);
		return 0;
	}
	int ok = 0;
	{
		char utoken[token.size];
		String putoken = (String){.data=utoken, .size=sizeof(utoken)};
		string_unescape(&putoken, &token);
		char url[1024];
		int n = snprintf(
			url, sizeof(url),
			"http://usher.twitch.tv/api/channel/hls/%.*s.m3u8"
			"?player=twitchweb&token=%.*s&sig=%.*s"
			"&allow_audio_only=true&allow_source=true&type=any&p=%u",
			channel.size, channel.data,
			putoken.size, putoken.data,
			sig.size, sig.data,
			rand_limit(10000000)
		);
		chunk.size = 0;
		if (n < 0 || n >= sizeof(url) || !http_get(url, &chunk)) {
			goto end;	
		}
	}
	{
		String m3u8 = (String){.data=chunk.data, .size=chunk.size};
		if (find_stream_url(stream, m3u8, q)) {
			ok = 1;
		}
	}
end:
	free(chunk.data);
	return ok;
}
