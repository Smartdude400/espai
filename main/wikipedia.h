#pragma once
#include <stdbool.h>

typedef struct {
    char title[128];
    char extract[2048];   // plain-text summary (first paragraphs)
    char image_url[300];  // lead-image thumbnail URL ("" if none)
} wiki_article_t;

// Fetch the summary of a specific page title (e.g. "Mount Everest").
bool wiki_fetch_summary(const char *topic, wiki_article_t *out);

// Fetch a random article summary.
bool wiki_fetch_random(wiki_article_t *out);

// Fuzzy full-text search: find the best-matching article for a (possibly
// misspelled) query and return its summary. Used by spelled voice search.
bool wiki_search(const char *query, wiki_article_t *out);

// Count how many images the article "title" has (via the media-list API).
// Returns 0 on error / none.
int wiki_count_images(const char *title);

// Max length of an image URL stored by wiki_media_images().
#define WIKI_IMG_URL_LEN 300

// Fill up to `max` image URLs from the article's media-list into `urls`.
// Returns the TOTAL number of images in the article (may exceed `max`).
int wiki_media_images(const char *title, char urls[][WIKI_IMG_URL_LEN], int max);
