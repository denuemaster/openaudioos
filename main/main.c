#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "driver/i2s_std.h"

#define SETUP_AP_SSID      "OpenAudioOS-Setup"
#define SETUP_AP_PASSWORD  "openaudio"

#define I2S_BCLK_GPIO  11
#define I2S_DOUT_GPIO  12
#define I2S_LRCK_GPIO  13
#define SAMPLE_RATE    44100

static const char *TAG = "OpenAudioOS";
static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static esp_ip4_addr_t current_ip;
static i2s_chan_handle_t tx_chan = NULL;
static bool sta_configured = false;
static char saved_ssid[33] = {0};
static char saved_password[65] = {0};

static SemaphoreHandle_t audio_mutex;
typedef struct {
    bool enabled;
    int frequency_hz;
    int volume;
    uint64_t frames_written;
    uint32_t underruns;
} audio_state_t;
static audio_state_t audio_state = {.enabled=true, .frequency_hz=440, .volume=25};

static const char *setup_page =
"<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>OpenAudioOS Setup</title><style>body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;background:#111;color:#eee;padding:32px}"
".card{max-width:620px;margin:auto;background:#1d1d1f;border-radius:18px;padding:28px}input,button{width:100%;box-sizing:border-box;padding:14px;margin:8px 0;border-radius:10px;border:0;font-size:16px}button{background:#0a84ff;color:white;font-weight:700}</style></head>"
"<body><div class='card'><h1>OpenAudioOS Setup</h1><p>Enter WiFi credentials. Device will reboot after saving.</p>"
"<form method='POST' action='/save'><input name='ssid' placeholder='WiFi SSID' required><input name='password' placeholder='WiFi Password' type='password'><button type='submit'>Save WiFi</button></form>"
"<p><a style='color:#8ab4ff' href='/api/status'>Status JSON</a></p></div></body></html>";

static const char *web_page =
"<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>OpenAudioOS M0.2</title><style>body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;background:#111;color:#eee;padding:24px;margin:0}.card{max-width:820px;margin:auto;background:#1d1d1f;border-radius:18px;padding:28px;box-shadow:0 20px 60px #0008}"
"input,button{box-sizing:border-box;padding:12px;margin:6px 0;border-radius:10px;border:0;font-size:16px}button{background:#0a84ff;color:white;font-weight:700;cursor:pointer}.secondary{background:#333}.row{display:flex;gap:10px;flex-wrap:wrap}.row>*{flex:1;min-width:160px}.ok{color:#5cff9d}.muted{color:#aaa}pre{background:#101010;padding:16px;border-radius:12px;overflow:auto}a{color:#8ab4ff}</style></head>"
"<body><div class='card'><h1>OpenAudioOS M0.2</h1><p class='ok'>Hardware validation running</p><div class='row'><button onclick='api(\"/api/audio/start\")'>Start tone</button><button class='secondary' onclick='api(\"/api/audio/stop\")'>Stop tone</button><button class='secondary' onclick='location.href=\"/reset-wifi\"'>Reset WiFi</button></div>"
"<h2>Audio Control</h2><label>Volume <span id='volLabel'></span></label><input id='vol' type='range' min='0' max='100' value='25' oninput='setVolume(this.value)'><label>Frequency Hz</label><div class='row'><input id='freq' type='number' value='440' min='50' max='20000'><button onclick='setFreq()'>Set frequency</button></div>"
"<h2>Status</h2><pre id='status'>Loading...</pre><p class='muted'>API: <a href='/api/status'>/api/status</a></p></div><script>async function refresh(){let r=await fetch('/api/status');let j=await r.json();document.getElementById('status').textContent=JSON.stringify(j,null,2);document.getElementById('vol').value=j.audio.volume;document.getElementById('volLabel').textContent=j.audio.volume+'%';document.getElementById('freq').value=j.audio.frequency_hz;}async function api(u){await fetch(u);refresh();}async function setVolume(v){document.getElementById('volLabel').textContent=v+'%';await fetch('/api/audio/volume?value='+v);refresh();}async function setFreq(){let v=document.getElementById('freq').value;await fetch('/api/audio/frequency?value='+v);refresh();}setInterval(refresh,3000);refresh();</script></body></html>";

static esp_err_t load_wifi_config(void){nvs_handle_t nvs;esp_err_t err=nvs_open("oaos",NVS_READONLY,&nvs);if(err!=ESP_OK)return err;size_t sl=sizeof(saved_ssid),pl=sizeof(saved_password);err=nvs_get_str(nvs,"ssid",saved_ssid,&sl);if(err==ESP_OK)err=nvs_get_str(nvs,"pass",saved_password,&pl);nvs_close(nvs);if(err==ESP_OK&&strlen(saved_ssid)>0){sta_configured=true;ESP_LOGI(TAG,"Loaded WiFi config for SSID: %s",saved_ssid);return ESP_OK;}return ESP_FAIL;}
static esp_err_t save_wifi_config(const char *ssid,const char *password){nvs_handle_t nvs;ESP_RETURN_ON_ERROR(nvs_open("oaos",NVS_READWRITE,&nvs),TAG,"nvs_open failed");ESP_RETURN_ON_ERROR(nvs_set_str(nvs,"ssid",ssid),TAG,"nvs ssid failed");ESP_RETURN_ON_ERROR(nvs_set_str(nvs,"pass",password?password:""),TAG,"nvs pass failed");ESP_RETURN_ON_ERROR(nvs_commit(nvs),TAG,"nvs commit failed");nvs_close(nvs);return ESP_OK;}

static void wifi_event_handler(void *arg,esp_event_base_t event_base,int32_t event_id,void *event_data){
 if(event_base==WIFI_EVENT&&event_id==WIFI_EVENT_STA_START){ESP_LOGI(TAG,"WiFi STA started, connecting to %s",saved_ssid);esp_wifi_connect();}
 else if(event_base==WIFI_EVENT&&event_id==WIFI_EVENT_STA_DISCONNECTED){wifi_event_sta_disconnected_t *d=(wifi_event_sta_disconnected_t*)event_data;ESP_LOGW(TAG,"WiFi disconnected, reason=%d. Reconnecting...",d?d->reason:-1);xEventGroupClearBits(wifi_event_group,WIFI_CONNECTED_BIT);if(sta_configured)esp_wifi_connect();}
 else if(event_base==WIFI_EVENT&&event_id==WIFI_EVENT_AP_START){ESP_LOGI(TAG,"Setup AP started: %s / password: %s",SETUP_AP_SSID,SETUP_AP_PASSWORD);ESP_LOGI(TAG,"Open http://192.168.4.1");}
 else if(event_base==IP_EVENT&&event_id==IP_EVENT_STA_GOT_IP){ip_event_got_ip_t *e=(ip_event_got_ip_t*)event_data;current_ip=e->ip_info.ip;ESP_LOGI(TAG,"Got IP: " IPSTR,IP2STR(&current_ip));xEventGroupSetBits(wifi_event_group,WIFI_CONNECTED_BIT);}}

static esp_err_t start_wifi(void){wifi_event_group=xEventGroupCreate();ESP_RETURN_ON_FALSE(wifi_event_group!=NULL,ESP_ERR_NO_MEM,TAG,"event group failed");ESP_RETURN_ON_ERROR(esp_netif_init(),TAG,"netif failed");ESP_RETURN_ON_ERROR(esp_event_loop_create_default(),TAG,"event loop failed");esp_netif_create_default_wifi_sta();esp_netif_create_default_wifi_ap();wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg),TAG,"wifi init failed");ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,&wifi_event_handler,NULL),TAG,"wifi handler failed");ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,&wifi_event_handler,NULL),TAG,"ip handler failed");wifi_config_t ap={0};strlcpy((char*)ap.ap.ssid,SETUP_AP_SSID,sizeof(ap.ap.ssid));strlcpy((char*)ap.ap.password,SETUP_AP_PASSWORD,sizeof(ap.ap.password));ap.ap.ssid_len=strlen(SETUP_AP_SSID);ap.ap.channel=1;ap.ap.max_connection=4;ap.ap.authmode=WIFI_AUTH_WPA2_PSK;if(sta_configured){wifi_config_t sta={0};strlcpy((char*)sta.sta.ssid,saved_ssid,sizeof(sta.sta.ssid));strlcpy((char*)sta.sta.password,saved_password,sizeof(sta.sta.password));ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA),TAG,"mode failed");ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA,&sta),TAG,"sta cfg failed");ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP,&ap),TAG,"ap cfg failed");}else{ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP),TAG,"ap mode failed");ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP,&ap),TAG,"ap cfg failed");}ESP_RETURN_ON_ERROR(esp_wifi_start(),TAG,"wifi start failed");return ESP_OK;}

static void url_decode(char *dst,const char *src,size_t dst_len){size_t di=0;for(size_t si=0;src[si]&&di+1<dst_len;si++){if(src[si]=='%'&&src[si+1]&&src[si+2]){char h[3]={src[si+1],src[si+2],0};dst[di++]=(char)strtol(h,NULL,16);si+=2;}else if(src[si]=='+')dst[di++]=' ';else dst[di++]=src[si];}dst[di]=0;}
static void form_get_value(const char *body,const char *key,char *out,size_t out_len){out[0]=0;char pat[32];snprintf(pat,sizeof(pat),"%s=",key);const char *s=strstr(body,pat);if(!s)return;s+=strlen(pat);const char *e=strchr(s,'&');size_t len=e?(size_t)(e-s):strlen(s);char tmp[128]={0};if(len>=sizeof(tmp))len=sizeof(tmp)-1;memcpy(tmp,s,len);url_decode(out,tmp,out_len);}
static bool query_int(httpd_req_t *req,const char *key,int *out){char q[128]={0};if(httpd_req_get_url_query_str(req,q,sizeof(q))!=ESP_OK)return false;char v[32]={0};if(httpd_query_key_value(q,key,v,sizeof(v))!=ESP_OK)return false;*out=atoi(v);return true;}

static esp_err_t root_handler(httpd_req_t *req){httpd_resp_set_type(req,"text/html; charset=utf-8");return httpd_resp_send(req,(sta_configured&&current_ip.addr!=0)?web_page:setup_page,HTTPD_RESP_USE_STRLEN);} 
static esp_err_t save_handler(httpd_req_t *req){char body[256]={0};int r=httpd_req_recv(req,body,sizeof(body)-1);if(r<=0){httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"No body");return ESP_FAIL;}char ssid[33]={0},pass[65]={0};form_get_value(body,"ssid",ssid,sizeof(ssid));form_get_value(body,"password",pass,sizeof(pass));if(strlen(ssid)==0){httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"SSID required");return ESP_FAIL;}ESP_ERROR_CHECK(save_wifi_config(ssid,pass));httpd_resp_sendstr(req,"<html><body><h1>Saved</h1><p>Rebooting.</p></body></html>");vTaskDelay(pdMS_TO_TICKS(700));esp_restart();return ESP_OK;}
static esp_err_t reset_wifi_handler(httpd_req_t *req){nvs_handle_t nvs;if(nvs_open("oaos",NVS_READWRITE,&nvs)==ESP_OK){nvs_erase_key(nvs,"ssid");nvs_erase_key(nvs,"pass");nvs_commit(nvs);nvs_close(nvs);}httpd_resp_sendstr(req,"WiFi reset. Rebooting.");vTaskDelay(pdMS_TO_TICKS(700));esp_restart();return ESP_OK;}
static esp_err_t audio_start_handler(httpd_req_t *req){xSemaphoreTake(audio_mutex,portMAX_DELAY);audio_state.enabled=true;xSemaphoreGive(audio_mutex);httpd_resp_sendstr(req,"OK");return ESP_OK;}
static esp_err_t audio_stop_handler(httpd_req_t *req){xSemaphoreTake(audio_mutex,portMAX_DELAY);audio_state.enabled=false;xSemaphoreGive(audio_mutex);httpd_resp_sendstr(req,"OK");return ESP_OK;}
static esp_err_t audio_volume_handler(httpd_req_t *req){int v;if(!query_int(req,"value",&v)){httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"missing value");return ESP_FAIL;}if(v<0)v=0;if(v>100)v=100;xSemaphoreTake(audio_mutex,portMAX_DELAY);audio_state.volume=v;xSemaphoreGive(audio_mutex);httpd_resp_sendstr(req,"OK");return ESP_OK;}
static esp_err_t audio_frequency_handler(httpd_req_t *req){int v;if(!query_int(req,"value",&v)){httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"missing value");return ESP_FAIL;}if(v<50)v=50;if(v>20000)v=20000;xSemaphoreTake(audio_mutex,portMAX_DELAY);audio_state.frequency_hz=v;xSemaphoreGive(audio_mutex);httpd_resp_sendstr(req,"OK");return ESP_OK;}
static esp_err_t api_status_handler(httpd_req_t *req){audio_state_t s;xSemaphoreTake(audio_mutex,portMAX_DELAY);s=audio_state;xSemaphoreGive(audio_mutex);char json[768];snprintf(json,sizeof(json),"{\"name\":\"OpenAudioOS M0.2\",\"configured\":%s,\"ssid\":\"%s\",\"ip\":\"" IPSTR "\",\"uptime_ms\":%lld,\"free_heap\":%lu,\"free_psram\":%lu,\"audio\":{\"enabled\":%s,\"frequency_hz\":%d,\"volume\":%d,\"frames_written\":%llu,\"underruns\":%lu,\"sample_rate\":%d,\"i2s_bclk\":%d,\"i2s_dout\":%d,\"i2s_lrck\":%d}}",sta_configured?"true":"false",saved_ssid,IP2STR(&current_ip),(long long)(esp_timer_get_time()/1000),(unsigned long)esp_get_free_heap_size(),(unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),s.enabled?"true":"false",s.frequency_hz,s.volume,(unsigned long long)s.frames_written,(unsigned long)s.underruns,SAMPLE_RATE,I2S_BCLK_GPIO,I2S_DOUT_GPIO,I2S_LRCK_GPIO);httpd_resp_set_type(req,"application/json");return httpd_resp_send(req,json,HTTPD_RESP_USE_STRLEN);}

static esp_err_t start_webserver(void){httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.server_port=80;cfg.stack_size=8192;cfg.max_uri_handlers=10;httpd_handle_t server=NULL;ESP_RETURN_ON_ERROR(httpd_start(&server,&cfg),TAG,"httpd_start failed");httpd_uri_t routes[]={{.uri="/",.method=HTTP_GET,.handler=root_handler},{.uri="/save",.method=HTTP_POST,.handler=save_handler},{.uri="/reset-wifi",.method=HTTP_GET,.handler=reset_wifi_handler},{.uri="/api/status",.method=HTTP_GET,.handler=api_status_handler},{.uri="/api/audio/start",.method=HTTP_GET,.handler=audio_start_handler},{.uri="/api/audio/stop",.method=HTTP_GET,.handler=audio_stop_handler},{.uri="/api/audio/volume",.method=HTTP_GET,.handler=audio_volume_handler},{.uri="/api/audio/frequency",.method=HTTP_GET,.handler=audio_frequency_handler}};for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++)ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server,&routes[i]),TAG,"route failed");ESP_LOGI(TAG,"HTTP server started on port 80");return ESP_OK;}

static esp_err_t init_i2s(void){i2s_chan_config_t c=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);c.dma_desc_num=8;c.dma_frame_num=512;ESP_RETURN_ON_ERROR(i2s_new_channel(&c,&tx_chan,NULL),TAG,"i2s_new_channel failed");i2s_std_config_t s={.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO),.gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=I2S_BCLK_GPIO,.ws=I2S_LRCK_GPIO,.dout=I2S_DOUT_GPIO,.din=I2S_GPIO_UNUSED,.invert_flags={.mclk_inv=false,.bclk_inv=false,.ws_inv=false}}};ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_chan,&s),TAG,"i2s init failed");ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_chan),TAG,"i2s enable failed");ESP_LOGI(TAG,"I2S initialized: BCLK=%d DOUT=%d LRCK=%d sample_rate=%d",I2S_BCLK_GPIO,I2S_DOUT_GPIO,I2S_LRCK_GPIO,SAMPLE_RATE);return ESP_OK;}

static void audio_task(void *arg){const int frames=256;int16_t buffer[frames*2];double phase=0.0;ESP_LOGI(TAG,"Audio engine started");while(true){audio_state_t s;xSemaphoreTake(audio_mutex,portMAX_DELAY);s=audio_state;xSemaphoreGive(audio_mutex);double step=2.0*M_PI*(double)s.frequency_hz/(double)SAMPLE_RATE;int amp=(s.volume*30000)/100;for(int i=0;i<frames;i++){int16_t sample=0;if(s.enabled&&s.volume>0)sample=(int16_t)(sin(phase)*amp);phase+=step;if(phase>=2.0*M_PI)phase-=2.0*M_PI;buffer[i*2]=sample;buffer[i*2+1]=sample;}size_t written=0;esp_err_t err=i2s_channel_write(tx_chan,buffer,sizeof(buffer),&written,portMAX_DELAY);xSemaphoreTake(audio_mutex,portMAX_DELAY);if(err==ESP_OK&&written==sizeof(buffer))audio_state.frames_written+=frames;else audio_state.underruns++;xSemaphoreGive(audio_mutex);}}
static void status_task(void *arg){while(true){audio_state_t s;xSemaphoreTake(audio_mutex,portMAX_DELAY);s=audio_state;xSemaphoreGive(audio_mutex);ESP_LOGI(TAG,"Status: ssid=%s heap=%lu psram=%lu uptime=%lldms ip=" IPSTR " audio=%s vol=%d freq=%d frames=%llu underruns=%lu",saved_ssid,(unsigned long)esp_get_free_heap_size(),(unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(long long)(esp_timer_get_time()/1000),IP2STR(&current_ip),s.enabled?"on":"off",s.volume,s.frequency_hz,(unsigned long long)s.frames_written,(unsigned long)s.underruns);vTaskDelay(pdMS_TO_TICKS(10000));}}

void app_main(void){ESP_LOGI(TAG,"OpenAudioOS M0.2 starting");ESP_LOGI(TAG,"Free heap: %lu",(unsigned long)esp_get_free_heap_size());ESP_LOGI(TAG,"Free PSRAM: %lu",(unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));audio_mutex=xSemaphoreCreateMutex();ESP_ERROR_CHECK(audio_mutex?ESP_OK:ESP_ERR_NO_MEM);esp_err_t ret=nvs_flash_init();if(ret==ESP_ERR_NVS_NO_FREE_PAGES||ret==ESP_ERR_NVS_NEW_VERSION_FOUND){ESP_ERROR_CHECK(nvs_flash_erase());ESP_ERROR_CHECK(nvs_flash_init());}else ESP_ERROR_CHECK(ret);load_wifi_config();ESP_ERROR_CHECK(start_wifi());ESP_ERROR_CHECK(start_webserver());ESP_ERROR_CHECK(init_i2s());xTaskCreatePinnedToCore(audio_task,"audio_task",4096,NULL,20,NULL,1);xTaskCreate(status_task,"status_task",4096,NULL,5,NULL);}
