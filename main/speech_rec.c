#include "speech_rec.h"
#include "audio_mic.h"
#include "app_config.h"

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "model_path.h"
#include "esp_mn_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_speech_commands.h"

static const char *TAG = "speech";

static esp_mn_iface_t   *s_mn;
static model_iface_data_t *s_mn_data;
static int s_chunk;       // samples MultiNet wants per detect() call
static int s_rate = MIC_SAMPLE_RATE;

// Spoken phrase -> command ID. Letters use the NATO phonetic alphabet so they
// are acoustically distinct (saying "B/D/E/G/P/T" would confuse the recogniser).
typedef struct { int id; const char *phrase; } cmd_def_t;
static const cmd_def_t k_cmds[] = {
    {  1, "alpha" },    {  2, "bravo" },   {  3, "charlie" }, {  4, "delta" },
    {  5, "echo" },     {  6, "foxtrot" }, {  7, "golf" },    {  8, "hotel" },
    {  9, "india" },    { 10, "juliet" },  { 11, "kilo" },    { 12, "lima" },
    { 13, "mike" },     { 14, "november"}, { 15, "oscar" },   { 16, "papa" },
    { 17, "quebec" },   { 18, "romeo" },   { 19, "sierra" },  { 20, "tango" },
    { 21, "uniform" },  { 22, "victor" },  { 23, "whiskey" }, { 24, "x ray" },
    { 25, "yankee" },   { 26, "zulu" },
    // control / shortcut words (with a couple of aliases)
    { CMD_SEARCH, "search" },   { CMD_SEARCH, "look up" },
    { CMD_DELETE, "delete" },   { CMD_DELETE, "back space" },
    { CMD_CLEAR,  "clear" },
    { CMD_SPACE,  "space" },
    { CMD_RANDOM, "random article" }, { CMD_RANDOM, "surprise me" },
    { CMD_BACK,   "go back" },
    { CMD_MAZE,   "play maze" },
    { CMD_2048,   "play puzzle" },
    { CMD_EXIT_GAME, "exit game" },
};

bool speech_init(void)
{
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models || models->num <= 0) {
        ESP_LOGE(TAG, "no speech models found in 'model' partition");
        return false;
    }
    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (!mn_name) {
        ESP_LOGE(TAG, "no English MultiNet model in flash");
        return false;
    }
    ESP_LOGI(TAG, "loading model: %s", mn_name);

    s_mn = esp_mn_handle_from_name(mn_name);
    s_mn_data = s_mn->create(mn_name, 5800);   // 5.8s command timeout
    if (!s_mn_data) {
        ESP_LOGE(TAG, "multinet create failed");
        return false;
    }
    s_mn->set_det_threshold(s_mn_data, 0.4f);
    s_chunk = s_mn->get_samp_chunksize(s_mn_data);
    s_rate  = s_mn->get_samp_rate(s_mn_data);

    // Register the command phrases.
    esp_mn_commands_alloc(s_mn, s_mn_data);
    esp_mn_commands_clear();
    for (size_t i = 0; i < sizeof(k_cmds) / sizeof(k_cmds[0]); i++) {
        esp_mn_commands_add(k_cmds[i].id, k_cmds[i].phrase);
    }
    esp_mn_error_t *err = esp_mn_commands_update();
    if (err && err->num > 0) {
        for (int i = 0; i < err->num; i++) {
            ESP_LOGW(TAG, "phrase could not be added: \"%s\"", err->phrases[i]->string);
        }
    }
    s_mn->print_active_speech_commands(s_mn_data);
    ESP_LOGI(TAG, "MultiNet ready (chunk=%d samples, rate=%d Hz)", s_chunk, s_rate);
    return true;
}

int speech_phrase_to_id(const char *phrase)
{
    for (size_t i = 0; i < sizeof(k_cmds) / sizeof(k_cmds[0]); i++) {
        if (strcmp(k_cmds[i].phrase, phrase) == 0) return k_cmds[i].id;
    }
    return -1;
}

int speech_listen(int max_ms)
{
    if (!s_mn_data) return -1;

    int16_t *buf = malloc((size_t)s_chunk * sizeof(int16_t));
    if (!buf) return -1;

    s_mn->clean(s_mn_data);   // reset recogniser state for a fresh utterance

    int fed_ms = 0;
    int result = -1;
    const int chunk_ms = s_chunk * 1000 / s_rate;

    while (fed_ms < max_ms) {
        size_t n = mic_read(buf, (size_t)s_chunk);
        if (n < (size_t)s_chunk) {
            memset(buf + n, 0, ((size_t)s_chunk - n) * sizeof(int16_t));
        }
        esp_mn_state_t st = s_mn->detect(s_mn_data, buf);
        fed_ms += chunk_ms;

        if (st == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t *r = s_mn->get_results(s_mn_data);
            if (r && r->num > 0) {
                result = r->command_id[0];
                ESP_LOGI(TAG, "command id=%d (phrase %d, prob %d%%): %s",
                         r->command_id[0], r->phrase_id[0],
                         (int)(r->prob[0] * 100), r->string);
            }
            break;
        } else if (st == ESP_MN_STATE_TIMEOUT) {
            ESP_LOGI(TAG, "listen timeout");
            break;
        }
    }
    free(buf);
    return result;
}
