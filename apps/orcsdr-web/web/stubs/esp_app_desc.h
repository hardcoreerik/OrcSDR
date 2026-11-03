#pragma once
typedef struct { char version[32]; char project_name[32]; char date[16]; char time[16]; } esp_app_desc_t;
static inline const esp_app_desc_t* esp_app_get_description() {
  static const esp_app_desc_t d = {"web", "OrcSDR Web", "", ""};
  return &d;
}
