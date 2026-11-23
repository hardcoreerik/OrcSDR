#include "eth_app.hpp"
#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "orcsdr_board_profile.h"
#include "sdkconfig.h"

#if CONFIG_ORCSDR_ETHERNET
#include "esp_event.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "soc/soc_caps.h"
#endif

namespace eth_app {
namespace {
constexpr char TAG[] = "ETH";
Status g_status;
#if CONFIG_ORCSDR_ETHERNET
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
esp_eth_handle_t g_handle = nullptr;
esp_netif_t* g_netif = nullptr;

void set_error(const char* text) {
  portENTER_CRITICAL(&g_lock);
  std::snprintf(g_status.error, sizeof(g_status.error), "%s", text);
  portEXIT_CRITICAL(&g_lock);
}

void on_eth_event(void*, esp_event_base_t, int32_t id, void* data) {
  esp_eth_handle_t handle = *static_cast<esp_eth_handle_t*>(data);
  if (id == ETHERNET_EVENT_CONNECTED) {
    eth_speed_t speed = ETH_SPEED_10M;
    eth_duplex_t duplex = ETH_DUPLEX_HALF;
    (void)esp_eth_ioctl(handle, ETH_CMD_G_SPEED, &speed);
    (void)esp_eth_ioctl(handle, ETH_CMD_G_DUPLEX_MODE, &duplex);
    uint8_t mac[6] = {};
    (void)esp_eth_ioctl(handle, ETH_CMD_G_MAC_ADDR, mac);
    portENTER_CRITICAL(&g_lock);
    g_status.link_up = true;
    g_status.speed_mbps = speed == ETH_SPEED_100M ? 100 : 10;
    g_status.full_duplex = duplex == ETH_DUPLEX_FULL;
    ++g_status.link_changes;
    std::snprintf(g_status.mac, sizeof(g_status.mac), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    portEXIT_CRITICAL(&g_lock);
    ESP_LOGI(TAG, "ETH_LINK_UP speed=%d duplex=%s mac=%s", g_status.speed_mbps, g_status.full_duplex ? "full" : "half", g_status.mac);
  } else if (id == ETHERNET_EVENT_DISCONNECTED) {
    portENTER_CRITICAL(&g_lock);
    g_status.link_up = false;
    g_status.got_ip = false;
    g_status.ip[0] = g_status.gateway[0] = '\0';
    ++g_status.link_changes;
    portEXIT_CRITICAL(&g_lock);
    ESP_LOGW(TAG, "ETH_LINK_DOWN (cable unplugged or no link partner)");
  } else if (id == ETHERNET_EVENT_START) {
    ESP_LOGI(TAG, "ETH_STARTED");
  } else if (id == ETHERNET_EVENT_STOP) {
    ESP_LOGW(TAG, "ETH_STOPPED");
  }
}

void on_ip_event(void*, esp_event_base_t, int32_t id, void* data) {
  if (id == IP_EVENT_ETH_GOT_IP) {
    const ip_event_got_ip_t* e = static_cast<const ip_event_got_ip_t*>(data);
    portENTER_CRITICAL(&g_lock);
    g_status.got_ip = true;
    std::snprintf(g_status.ip, sizeof(g_status.ip), IPSTR, IP2STR(&e->ip_info.ip));
    std::snprintf(g_status.gateway, sizeof(g_status.gateway), IPSTR, IP2STR(&e->ip_info.gw));
    portEXIT_CRITICAL(&g_lock);
    ESP_LOGI(TAG, "ETH_GOT_IP ip=%s gateway=%s", g_status.ip, g_status.gateway);
  } else if (id == IP_EVENT_ETH_LOST_IP) {
    portENTER_CRITICAL(&g_lock);
    g_status.got_ip = false;
    g_status.ip[0] = g_status.gateway[0] = '\0';
    portEXIT_CRITICAL(&g_lock);
    ESP_LOGW(TAG, "ETH_LOST_IP");
  }
}

// Builds the MAC and PHY from the board profile and installs the driver. `generic` selects the vendor-neutral PHY driver.
esp_err_t install(const orcsdr_board_profile_t* b, bool generic) {
  // Filled in field by field: ESP-IDF's ETH_ESP32_EMAC_DEFAULT_CONFIG() for the P4 lists its members out of declaration order, which C++ rejects.
  // The values it would give (and that the profile repeats): RMII, DMA burst 32, default interrupt priority, MDC clock at the driver default.
  eth_esp32_emac_config_t emac = {};
  emac.smi_gpio.mdc_num = b->eth_mdc;
  emac.smi_gpio.mdio_num = b->eth_mdio;
  emac.interface = EMAC_DATA_INTERFACE_RMII;
  emac.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;  // the PHY supplies the 50 MHz reference
  emac.clock_config.rmii.clock_gpio = static_cast<emac_rmii_clock_gpio_t>(b->eth_rmii_clk_in);
  emac.dma_burst_len = ETH_DMA_BURST_LEN_32;
  emac.intr_priority = 0;
  emac.mdc_freq_hz = 0;
#if !SOC_EMAC_RMII_CLK_OUT_INTERNAL_LOOPBACK
  emac.clock_config_out_in.rmii.clock_mode = EMAC_CLK_EXT_IN;
  emac.clock_config_out_in.rmii.clock_gpio = static_cast<emac_rmii_clock_gpio_t>(-1);
#endif
  emac.emac_dataif_gpio.rmii.tx_en_num = b->eth_tx_en;
  emac.emac_dataif_gpio.rmii.txd0_num = b->eth_txd0;
  emac.emac_dataif_gpio.rmii.txd1_num = b->eth_txd1;
  emac.emac_dataif_gpio.rmii.crs_dv_num = b->eth_crs_dv;
  emac.emac_dataif_gpio.rmii.rxd0_num = b->eth_rxd0;
  emac.emac_dataif_gpio.rmii.rxd1_num = b->eth_rxd1;
  eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
  esp_eth_mac_t* mac = esp_eth_mac_new_esp32(&emac, &mac_cfg);
  eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
  phy_cfg.phy_addr = b->eth_phy_addr;
  phy_cfg.reset_gpio_num = b->eth_phy_reset;
  esp_eth_phy_t* phy = generic ? esp_eth_phy_new_generic(&phy_cfg) : esp_eth_phy_new_ip101(&phy_cfg);
  if (!mac || !phy) {
    if (mac) mac->del(mac);
    if (phy) phy->del(phy);
    return ESP_ERR_NO_MEM;
  }
  esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(mac, phy);
  const esp_err_t err = esp_eth_driver_install(&cfg, &g_handle);
  if (err != ESP_OK) {
    mac->del(mac);  // a failed install leaves both objects with the caller
    phy->del(phy);
    g_handle = nullptr;
  }
  return err;
}
#endif
}  // namespace

void start() {
#if CONFIG_ORCSDR_ETHERNET
  g_status.compiled = true;
  const orcsdr_board_profile_t* b = orcsdr_board_profile();
  if (!b->has_ethernet || b->eth_mdc < 0 || b->eth_mdio < 0 || b->eth_rmii_clk_in < 0 || b->eth_tx_en < 0 || b->eth_txd0 < 0 || b->eth_txd1 < 0 ||
      b->eth_crs_dv < 0 || b->eth_rxd0 < 0 || b->eth_rxd1 < 0) {
    set_error("board profile has no Ethernet pins");
    ESP_LOGW(TAG, "Ethernet is compiled in but board %s lists no Ethernet pins: not started", b->name);
    return;
  }
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_init());
  const esp_err_t loop = esp_event_loop_create_default();  // already created by Wi-Fi on boards that have it
  if (loop != ESP_OK && loop != ESP_ERR_INVALID_STATE) {
    set_error("event loop failed");
    ESP_LOGE(TAG, "event loop: %s", esp_err_to_name(loop));
    return;
  }
  const char* phy = "ip101";
  esp_err_t err = install(b, false);
  if (err != ESP_OK) {
    // The PHY on this board is documented, not confirmed: if the IP101 driver does not recognise it, try the vendor-neutral driver.
    ESP_LOGW(TAG, "IP101 PHY driver failed (%s) with MDC %d MDIO %d addr %d reset %d; trying the generic PHY driver", esp_err_to_name(err), b->eth_mdc, b->eth_mdio,
             b->eth_phy_addr, b->eth_phy_reset);
    phy = "generic";
    err = install(b, true);
  }
  if (err != ESP_OK) {
    set_error("driver install failed");
    ESP_LOGE(TAG, "Ethernet driver install failed: %s (pins from profile %s: MDC %d MDIO %d, PHY addr %d, reset %d)", esp_err_to_name(err), b->name, b->eth_mdc,
             b->eth_mdio, b->eth_phy_addr, b->eth_phy_reset);
    return;
  }
  esp_netif_config_t ncfg = ESP_NETIF_DEFAULT_ETH();
  g_netif = esp_netif_new(&ncfg);
  if (!g_netif || esp_netif_attach(g_netif, esp_eth_new_netif_glue(g_handle)) != ESP_OK) {
    set_error("netif attach failed");
    ESP_LOGE(TAG, "netif attach failed");
    return;
  }
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event, nullptr));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_ip_event, nullptr));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, on_ip_event, nullptr));
  portENTER_CRITICAL(&g_lock);
  std::snprintf(g_status.phy, sizeof(g_status.phy), "%s", phy);
  portEXIT_CRITICAL(&g_lock);
  const esp_err_t started = esp_eth_start(g_handle);
  if (started != ESP_OK) {
    set_error("start failed");
    ESP_LOGE(TAG, "esp_eth_start: %s", esp_err_to_name(started));
    return;
  }
  portENTER_CRITICAL(&g_lock);
  g_status.started = true;
  g_status.error[0] = '\0';
  portEXIT_CRITICAL(&g_lock);
  ESP_LOGI(TAG, "ETH_DRIVER_UP board=%s phy=%s addr=%d (profile pins; unverified on this board until a link comes up)", b->name, phy, b->eth_phy_addr);
#else
  ESP_LOGI(TAG, "Ethernet driver not included in this build (CONFIG_ORCSDR_ETHERNET is off)");
#endif
}

Status status() {
  Status s;
#if CONFIG_ORCSDR_ETHERNET
  portENTER_CRITICAL(&g_lock);
  s = g_status;
  portEXIT_CRITICAL(&g_lock);
#else
  s = g_status;
#endif
  return s;
}
}  // namespace eth_app
