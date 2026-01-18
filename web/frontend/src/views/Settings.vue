<template>
  <v-container fluid class="pa-6">
    <!-- Header with Save Actions -->
    <div class="d-flex align-center justify-space-between mb-6">
      <h1 class="text-h4">Settings</h1>
      <div>
        <v-btn
          variant="outlined"
          class="mr-3"
          @click="resetSettings"
        >
          Reset to Defaults
        </v-btn>
        <v-btn
          color="primary"
          prepend-icon="mdi-content-save"
          :loading="saving"
          @click="saveSettings"
        >
          Save All Settings
        </v-btn>
      </div>
    </div>

    <!-- ═══════════════════════════════════════════════════════════════════ -->
    <!-- NETWORK CONFIGURATION SECTION -->
    <!-- ═══════════════════════════════════════════════════════════════════ -->
    <div class="text-overline text-medium-emphasis mb-3 d-flex align-center">
      <v-icon icon="mdi-network" size="small" class="mr-2" />
      Network Configuration
    </div>

    <v-row class="mb-6">
      <!-- Control Interface -->
      <v-col cols="12" lg="6">
        <v-card variant="outlined" class="fill-height">
          <v-card-title class="d-flex align-center py-3 bg-grey-darken-4">
            <v-icon icon="mdi-ethernet" class="mr-2" color="primary" />
            Control Interface
            <v-chip size="x-small" class="ml-2" color="info" variant="tonal">Management</v-chip>
          </v-card-title>
          <v-card-text class="pt-4">
            <v-row dense>
              <v-col cols="12" sm="6">
                <v-select
                  v-model="networkConfig.interface"
                  label="Network Interface"
                  :items="interfaces"
                  item-title="display"
                  item-value="name"
                  variant="outlined"
                  density="compact"
                  hide-details="auto"
                />
              </v-col>
              <v-col cols="12" sm="6">
                <v-select
                  v-model="networkConfig.mode"
                  label="IP Mode"
                  :items="[
                    { title: 'DHCP', value: 'dhcp' },
                    { title: 'Static', value: 'static' }
                  ]"
                  item-title="title"
                  item-value="value"
                  variant="outlined"
                  density="compact"
                  hide-details
                />
              </v-col>
            </v-row>

            <v-expand-transition>
              <div v-if="networkConfig.mode === 'static'">
                <v-row dense class="mt-2">
                  <v-col cols="12" sm="4">
                    <v-text-field
                      v-model="networkConfig.ip_address"
                      label="IP Address"
                      placeholder="192.168.1.100"
                      variant="outlined"
                      density="compact"
                      hide-details
                    />
                  </v-col>
                  <v-col cols="12" sm="4">
                    <v-text-field
                      v-model="networkConfig.netmask"
                      label="Netmask"
                      placeholder="255.255.255.0"
                      variant="outlined"
                      density="compact"
                      hide-details
                    />
                  </v-col>
                  <v-col cols="12" sm="4">
                    <v-text-field
                      v-model="networkConfig.gateway"
                      label="Gateway"
                      placeholder="192.168.1.1"
                      variant="outlined"
                      density="compact"
                      hide-details
                    />
                  </v-col>
                </v-row>
                <v-row dense class="mt-2">
                  <v-col cols="12" sm="8">
                    <v-combobox
                      v-model="networkConfig.dns_servers"
                      label="DNS Servers"
                      multiple
                      chips
                      closable-chips
                      variant="outlined"
                      density="compact"
                      hide-details
                    />
                  </v-col>
                  <v-col cols="12" sm="4" class="d-flex align-center">
                    <v-checkbox
                      v-model="networkConfig.bind_services"
                      label="Bind to this IP only"
                      density="compact"
                      hide-details
                    />
                  </v-col>
                </v-row>
              </div>
            </v-expand-transition>

            <v-alert
              v-if="networkConfig.mode === 'static'"
              type="warning"
              variant="tonal"
              density="compact"
              class="mt-4"
            >
              Changing IP may disconnect you. Ensure you can access the new address.
            </v-alert>
          </v-card-text>
          <v-card-actions class="px-4 pb-4">
            <v-btn
              color="primary"
              variant="tonal"
              :loading="networkSaving"
              :disabled="!networkConfig.interface"
              @click="saveNetworkConfig"
            >
              Apply Control Network
            </v-btn>
          </v-card-actions>
        </v-card>
      </v-col>

      <!-- AES67 Interface -->
      <v-col cols="12" lg="6">
        <v-card variant="outlined" class="fill-height">
          <v-card-title class="d-flex align-center py-3 bg-grey-darken-4">
            <v-icon icon="mdi-waveform" class="mr-2" color="success" />
            AES67 Interface
            <v-chip size="x-small" class="ml-2" color="success" variant="tonal">Audio</v-chip>
          </v-card-title>
          <v-card-text class="pt-4">
            <v-row dense>
              <v-col cols="12" sm="6">
                <v-select
                  v-model="aes67Config.interface"
                  label="Network Interface"
                  :items="interfaces"
                  item-title="display"
                  item-value="name"
                  variant="outlined"
                  density="compact"
                  hide-details="auto"
                />
              </v-col>
              <v-col cols="12" sm="6">
                <v-select
                  v-model="aes67Config.mode"
                  label="IP Mode"
                  :items="[
                    { title: 'DHCP', value: 'dhcp' },
                    { title: 'Static', value: 'static' }
                  ]"
                  item-title="title"
                  item-value="value"
                  variant="outlined"
                  density="compact"
                  hide-details
                />
              </v-col>
            </v-row>

            <v-expand-transition>
              <div v-if="aes67Config.mode === 'static'">
                <v-row dense class="mt-2">
                  <v-col cols="12" sm="4">
                    <v-text-field
                      v-model="aes67Config.ip_address"
                      label="IP Address"
                      placeholder="192.168.2.100"
                      variant="outlined"
                      density="compact"
                      hide-details
                    />
                  </v-col>
                  <v-col cols="12" sm="4">
                    <v-text-field
                      v-model="aes67Config.netmask"
                      label="Netmask"
                      placeholder="255.255.255.0"
                      variant="outlined"
                      density="compact"
                      hide-details
                    />
                  </v-col>
                  <v-col cols="12" sm="4">
                    <v-text-field
                      v-model="aes67Config.gateway"
                      label="Gateway"
                      placeholder="192.168.2.1"
                      variant="outlined"
                      density="compact"
                      hide-details
                    />
                  </v-col>
                </v-row>
              </div>
            </v-expand-transition>

            <v-alert
              type="info"
              variant="tonal"
              density="compact"
              class="mt-4"
            >
              AES67 networks are typically isolated. Gateway and DNS are usually not required.
            </v-alert>
          </v-card-text>
          <v-card-actions class="px-4 pb-4">
            <v-btn
              color="success"
              variant="tonal"
              :loading="aes67Saving"
              :disabled="!aes67Config.interface"
              @click="saveAES67Config"
            >
              Apply AES67 Network
            </v-btn>
          </v-card-actions>
        </v-card>
      </v-col>
    </v-row>

    <!-- ═══════════════════════════════════════════════════════════════════ -->
    <!-- RECORDING SETTINGS SECTION -->
    <!-- ═══════════════════════════════════════════════════════════════════ -->
    <div class="text-overline text-medium-emphasis mb-3 d-flex align-center">
      <v-icon icon="mdi-record-rec" size="small" class="mr-2" />
      Recording Settings
    </div>

    <v-row class="mb-6">
      <!-- Archive Settings -->
      <v-col cols="12" md="6" lg="4">
        <v-card variant="outlined" class="fill-height">
          <v-card-title class="d-flex align-center py-3 bg-grey-darken-4">
            <v-icon icon="mdi-archive" class="mr-2" color="warning" />
            Archive Storage
          </v-card-title>
          <v-card-text class="pt-4">
            <v-text-field
              v-model="config.archiveRoot"
              label="Archive Directory"
              prepend-inner-icon="mdi-folder"
              variant="outlined"
              density="compact"
              class="mb-3"
            />
            <v-select
              v-model="config.archiveLayout"
              label="File Naming Layout"
              :items="layouts"
              item-title="title"
              item-value="value"
              variant="outlined"
              density="compact"
              class="mb-3"
            />
            <v-text-field
              v-model.number="config.archivePeriod"
              label="Rotation Period"
              type="number"
              suffix="seconds"
              variant="outlined"
              density="compact"
              hint="3600 = 1 hour, 86400 = 24 hours"
              persistent-hint
            />
          </v-card-text>
        </v-card>
      </v-col>

      <!-- Timing Settings -->
      <v-col cols="12" md="6" lg="4">
        <v-card variant="outlined" class="fill-height">
          <v-card-title class="d-flex align-center py-3 bg-grey-darken-4">
            <v-icon icon="mdi-clock-outline" class="mr-2" color="info" />
            Timing & Sync
          </v-card-title>
          <v-card-text class="pt-4">
            <v-select
              v-model="config.archiveClock"
              label="Clock Source"
              :items="[
                { title: 'Local Time', value: 'localtime' },
                { title: 'UTC', value: 'utc' },
                { title: 'PTP/TAI', value: 'ptp' }
              ]"
              item-title="title"
              item-value="value"
              variant="outlined"
              density="compact"
              class="mb-3"
            />
            <v-select
              v-model="config.ptpInterface"
              label="PTP Network Interface"
              :items="interfaces"
              item-title="display"
              item-value="name"
              variant="outlined"
              density="compact"
              hint="Leave empty to disable PTP sync"
              persistent-hint
              clearable
            />
          </v-card-text>
        </v-card>
      </v-col>

      <!-- System Info -->
      <v-col cols="12" md="12" lg="4">
        <v-card variant="outlined" class="fill-height">
          <v-card-title class="d-flex align-center py-3 bg-grey-darken-4">
            <v-icon icon="mdi-information-outline" class="mr-2" color="grey" />
            System Status
          </v-card-title>
          <v-card-text class="pt-4">
            <v-list density="compact" class="bg-transparent">
              <v-list-item class="px-0">
                <template v-slot:prepend>
                  <v-icon icon="mdi-tag" size="small" class="mr-2" />
                </template>
                <v-list-item-title class="text-caption text-medium-emphasis">Version</v-list-item-title>
                <v-list-item-subtitle class="text-body-2">Audyn v1.0.0</v-list-item-subtitle>
              </v-list-item>
              <v-list-item class="px-0">
                <template v-slot:prepend>
                  <v-icon icon="mdi-server" size="small" class="mr-2" />
                </template>
                <v-list-item-title class="text-caption text-medium-emphasis">Backend</v-list-item-title>
                <v-list-item-subtitle>
                  <v-chip color="success" size="x-small" variant="flat">Connected</v-chip>
                </v-list-item-subtitle>
              </v-list-item>
              <v-list-item class="px-0">
                <template v-slot:prepend>
                  <v-icon icon="mdi-database" size="small" class="mr-2" />
                </template>
                <v-list-item-title class="text-caption text-medium-emphasis">Archive</v-list-item-title>
                <v-list-item-subtitle class="text-body-2">
                  {{ stats.totalFiles }} files, {{ stats.totalSize }}
                </v-list-item-subtitle>
              </v-list-item>
            </v-list>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- ═══════════════════════════════════════════════════════════════════ -->
    <!-- VOICE ACTIVITY DETECTION SECTION -->
    <!-- ═══════════════════════════════════════════════════════════════════ -->
    <div class="text-overline text-medium-emphasis mb-3 d-flex align-center">
      <v-icon icon="mdi-microphone-variant" size="small" class="mr-2" />
      Voice Activity Detection
    </div>

    <v-row class="mb-6">
      <!-- VOX Facility -->
      <v-col cols="12" md="6">
        <v-card variant="outlined" class="fill-height">
          <v-card-title class="d-flex align-center py-3 bg-grey-darken-4">
            <v-icon icon="mdi-microphone-variant" class="mr-2" color="pink" />
            VOX Facility
            <v-chip
              v-if="config.voxFacilityEnabled"
              size="x-small"
              class="ml-2"
              color="success"
              variant="flat"
            >
              Enabled
            </v-chip>
          </v-card-title>
          <v-card-text class="pt-4">
            <v-checkbox
              v-model="config.voxFacilityEnabled"
              label="Enable VOX (Voice-Activated Recording)"
              hint="When enabled, individual recorders can use threshold-based recording with configurable hang time"
              persistent-hint
            />
            <v-alert
              type="info"
              variant="tonal"
              density="compact"
              class="mt-4"
            >
              VOX creates separate segment files for each speech burst instead of time-based rotation.
              Configure per-recorder settings in the Capture page.
            </v-alert>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- ═══════════════════════════════════════════════════════════════════ -->
    <!-- LOCAL PLAYBACK SECTION -->
    <!-- ═══════════════════════════════════════════════════════════════════ -->
    <div class="text-overline text-medium-emphasis mb-3 d-flex align-center">
      <v-icon icon="mdi-folder-play" size="small" class="mr-2" />
      Local Playback
    </div>

    <v-row class="mb-6">
      <v-col cols="12" md="6">
        <v-card variant="outlined" class="fill-height">
          <v-card-title class="d-flex align-center py-3 bg-grey-darken-4">
            <v-icon icon="mdi-folder-play" class="mr-2" color="amber" />
            Local Folder Access
            <v-chip
              v-if="localPlaybackStore.isAvailable"
              size="x-small"
              class="ml-2"
              color="success"
              variant="flat"
            >
              Connected
            </v-chip>
            <v-chip
              v-else-if="localPlaybackStore.enabled && !localPlaybackStore.hasPermission"
              size="x-small"
              class="ml-2"
              color="warning"
              variant="flat"
            >
              Permission Required
            </v-chip>
          </v-card-title>
          <v-card-text class="pt-4">
            <v-alert
              v-if="!localPlaybackStore.isSupported"
              type="warning"
              variant="tonal"
              density="compact"
              class="mb-4"
            >
              Local playback requires Chrome or Edge browser.
            </v-alert>

            <template v-if="localPlaybackStore.isSupported">
              <p class="text-body-2 mb-4">
                Connect a local folder containing synced recordings for buffer-free playback.
                Ideal for on-air use when files are synced via SMB or rsync.
              </p>

              <template v-if="localPlaybackStore.isAvailable">
                <v-list density="compact" class="mb-4">
                  <v-list-item>
                    <template #prepend>
                      <v-icon icon="mdi-folder" color="amber" />
                    </template>
                    <v-list-item-title>{{ localPlaybackStore.directoryName }}</v-list-item-title>
                    <v-list-item-subtitle>Connected folder</v-list-item-subtitle>
                  </v-list-item>
                </v-list>
                <v-btn
                  color="error"
                  variant="tonal"
                  @click="localPlaybackStore.disconnect()"
                  prepend-icon="mdi-link-off"
                >
                  Disconnect
                </v-btn>
              </template>

              <template v-else-if="localPlaybackStore.enabled && !localPlaybackStore.hasPermission">
                <v-alert type="info" variant="tonal" density="compact" class="mb-4">
                  Browser permission expired. Click below to re-grant access to
                  <strong>{{ localPlaybackStore.directoryName }}</strong>.
                </v-alert>
                <v-btn
                  color="primary"
                  variant="elevated"
                  @click="localPlaybackStore.requestPermission()"
                  prepend-icon="mdi-lock-open"
                >
                  Grant Permission
                </v-btn>
              </template>

              <template v-else>
                <v-btn
                  color="primary"
                  variant="elevated"
                  @click="localPlaybackStore.selectFolder()"
                  prepend-icon="mdi-folder-plus"
                >
                  Select Local Folder
                </v-btn>
              </template>

              <v-alert
                type="info"
                variant="tonal"
                density="compact"
                class="mt-4"
              >
                When playing files, Audyn will check the local folder first.
                If not found locally, streaming from server is used as fallback.
              </v-alert>
            </template>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- ═══════════════════════════════════════════════════════════════════ -->
    <!-- SYSTEM CONFIGURATION SECTION -->
    <!-- ═══════════════════════════════════════════════════════════════════ -->
    <div class="text-overline text-medium-emphasis mb-3 d-flex align-center">
      <v-icon icon="mdi-cog" size="small" class="mr-2" />
      System Configuration
    </div>

    <v-row class="mb-6">
      <!-- Hostname & Time -->
      <v-col cols="12" md="6">
        <v-card variant="outlined" class="fill-height">
          <v-card-title class="d-flex align-center py-3 bg-grey-darken-4">
            <v-icon icon="mdi-server-network" class="mr-2" color="purple" />
            Hostname & Time
          </v-card-title>
          <v-card-text class="pt-4">
            <v-text-field
              v-model="systemConfig.hostname"
              label="Hostname"
              variant="outlined"
              density="compact"
              hint="Requires restart to apply"
              persistent-hint
              class="mb-3"
            />
            <v-autocomplete
              v-model="systemConfig.timezone"
              :items="timezones"
              label="Timezone"
              variant="outlined"
              density="compact"
              class="mb-3"
            />
            <v-combobox
              v-model="systemConfig.ntp_servers"
              label="NTP Servers"
              multiple
              chips
              closable-chips
              variant="outlined"
              density="compact"
              hint="Press Enter to add a server"
              persistent-hint
            />
          </v-card-text>
        </v-card>
      </v-col>

      <!-- SSL Certificate -->
      <v-col cols="12" md="6">
        <v-card variant="outlined" class="fill-height">
          <v-card-title class="d-flex align-center py-3 bg-grey-darken-4">
            <v-icon icon="mdi-lock" class="mr-2" color="success" />
            SSL Certificate
            <v-chip
              v-if="sslConfig.enabled"
              size="x-small"
              class="ml-2"
              color="success"
              variant="flat"
            >
              Active
            </v-chip>
            <v-chip
              v-else
              size="x-small"
              class="ml-2"
              color="warning"
              variant="tonal"
            >
              HTTP Only
            </v-chip>
          </v-card-title>
          <v-card-text class="pt-4">
            <v-alert
              v-if="sslConfig.enabled"
              type="success"
              variant="tonal"
              density="compact"
              class="mb-4"
            >
              <strong>{{ sslConfig.domain }}</strong>
              <span class="text-caption ml-2">
                ({{ sslConfig.cert_type === 'letsencrypt' ? "Let's Encrypt" : 'Manual' }})
              </span>
              <span v-if="sslConfig.cert_expiry" class="text-caption ml-2">
                Expires {{ formatDate(sslConfig.cert_expiry) }}
              </span>
            </v-alert>

            <v-tabs v-model="sslTab" density="compact" class="mb-4">
              <v-tab value="letsencrypt" size="small">Let's Encrypt</v-tab>
              <v-tab value="manual" size="small">Manual Upload</v-tab>
            </v-tabs>

            <v-window v-model="sslTab">
              <v-window-item value="letsencrypt">
                <v-row dense>
                  <v-col cols="12">
                    <v-text-field
                      v-model="sslConfig.domain"
                      label="Domain Name"
                      placeholder="audyn.example.com"
                      variant="outlined"
                      density="compact"
                      class="mb-2"
                    />
                  </v-col>
                  <v-col cols="12">
                    <v-text-field
                      v-model="sslConfig.email"
                      label="Email"
                      type="email"
                      placeholder="admin@example.com"
                      variant="outlined"
                      density="compact"
                    />
                  </v-col>
                </v-row>
                <v-alert type="info" variant="tonal" density="compact" class="mt-3 mb-3">
                  Requires port 80 accessible from internet.
                </v-alert>
                <v-btn
                  color="primary"
                  variant="tonal"
                  size="small"
                  :loading="sslLoading"
                  :disabled="!sslConfig.domain || !sslConfig.email"
                  @click="enableSSL"
                >
                  {{ sslConfig.enabled && sslConfig.cert_type === 'letsencrypt' ? 'Renew' : 'Enable HTTPS' }}
                </v-btn>
              </v-window-item>

              <v-window-item value="manual">
                <v-row dense>
                  <v-col cols="12">
                    <v-text-field
                      v-model="manualCert.domain"
                      label="Domain Name"
                      placeholder="audyn.example.com"
                      variant="outlined"
                      density="compact"
                      class="mb-2"
                    />
                  </v-col>
                  <v-col cols="12">
                    <v-file-input
                      v-model="manualCert.certFile"
                      label="Certificate (.crt, .pem)"
                      accept=".crt,.pem,.cer"
                      prepend-icon=""
                      prepend-inner-icon="mdi-certificate"
                      variant="outlined"
                      density="compact"
                      class="mb-2"
                    />
                  </v-col>
                  <v-col cols="12">
                    <v-file-input
                      v-model="manualCert.keyFile"
                      label="Private Key (.key, .pem)"
                      accept=".key,.pem"
                      prepend-icon=""
                      prepend-inner-icon="mdi-key"
                      variant="outlined"
                      density="compact"
                    />
                  </v-col>
                </v-row>
                <v-btn
                  color="primary"
                  variant="tonal"
                  size="small"
                  class="mt-3"
                  :loading="sslLoading"
                  :disabled="!manualCert.domain || !manualCert.certFile || !manualCert.keyFile"
                  @click="uploadCertificate"
                >
                  Upload Certificate
                </v-btn>
              </v-window-item>
            </v-window>

            <v-btn
              v-if="sslConfig.enabled"
              color="error"
              variant="text"
              size="small"
              class="mt-4"
              :loading="sslLoading"
              @click="disableSSL"
            >
              Disable SSL
            </v-btn>
          </v-card-text>
        </v-card>
      </v-col>

      <!-- Log Rotation -->
      <v-col cols="12" md="6">
        <v-card variant="outlined" class="fill-height">
          <v-card-title class="d-flex align-center py-3 bg-grey-darken-4">
            <v-icon icon="mdi-file-rotate-left" class="mr-2" color="orange" />
            Log Rotation
          </v-card-title>
          <v-card-text class="pt-4">
            <v-row dense>
              <v-col cols="12" sm="6">
                <v-select
                  v-model="logRotation.frequency"
                  label="Rotation Frequency"
                  :items="[
                    { title: 'Daily', value: 'daily' },
                    { title: 'Weekly', value: 'weekly' },
                    { title: 'Monthly', value: 'monthly' }
                  ]"
                  item-title="title"
                  item-value="value"
                  variant="outlined"
                  density="compact"
                  hide-details
                />
              </v-col>
              <v-col cols="12" sm="6">
                <v-text-field
                  v-model.number="logRotation.rotate_count"
                  label="Logs to Keep"
                  type="number"
                  min="1"
                  max="365"
                  variant="outlined"
                  density="compact"
                  hide-details
                />
              </v-col>
            </v-row>
            <v-row dense class="mt-3">
              <v-col cols="12" sm="6">
                <v-text-field
                  v-model="logRotation.max_size"
                  label="Max Size (optional)"
                  placeholder="100M"
                  hint="e.g., 100M, 1G"
                  variant="outlined"
                  density="compact"
                  persistent-hint
                />
              </v-col>
              <v-col cols="12" sm="6" class="d-flex align-center">
                <v-checkbox
                  v-model="logRotation.compress"
                  label="Compress rotated logs"
                  density="compact"
                  hide-details
                />
              </v-col>
            </v-row>
          </v-card-text>
          <v-card-actions class="px-4 pb-4">
            <v-btn
              color="primary"
              variant="tonal"
              size="small"
              :loading="logRotationSaving"
              @click="saveLogRotation"
            >
              Save Log Settings
            </v-btn>
            <v-btn
              color="secondary"
              variant="text"
              size="small"
              :loading="logRotationForcing"
              @click="forceLogRotation"
            >
              Rotate Now
            </v-btn>
          </v-card-actions>
        </v-card>
      </v-col>
    </v-row>

    <!-- ═══════════════════════════════════════════════════════════════════ -->
    <!-- SECURITY SECTION -->
    <!-- ═══════════════════════════════════════════════════════════════════ -->
    <div class="text-overline text-medium-emphasis mb-3 d-flex align-center">
      <v-icon icon="mdi-shield-lock" size="small" class="mr-2" />
      Security & Authentication
    </div>

    <v-row class="mb-6">
      <!-- Entra ID -->
      <v-col cols="12" lg="6">
        <v-card variant="outlined" class="fill-height">
          <v-card-title class="d-flex align-center py-3 bg-grey-darken-4">
            <v-icon icon="mdi-microsoft" class="mr-2" color="blue" />
            Microsoft Entra ID
          </v-card-title>
          <v-card-text class="pt-4">
            <v-row dense>
              <v-col cols="12" sm="6">
                <v-text-field
                  v-model="authConfig.entraTenantId"
                  label="Tenant ID"
                  placeholder="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
                  variant="outlined"
                  density="compact"
                />
              </v-col>
              <v-col cols="12" sm="6">
                <v-text-field
                  v-model="authConfig.entraClientId"
                  label="Client ID"
                  placeholder="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
                  variant="outlined"
                  density="compact"
                />
              </v-col>
              <v-col cols="12" sm="6">
                <v-text-field
                  v-model="authConfig.entraClientSecret"
                  label="Client Secret"
                  :type="showClientSecret ? 'text' : 'password'"
                  :append-inner-icon="showClientSecret ? 'mdi-eye-off' : 'mdi-eye'"
                  @click:append-inner="showClientSecret = !showClientSecret"
                  variant="outlined"
                  density="compact"
                  hint="Leave empty to keep existing"
                  persistent-hint
                />
              </v-col>
              <v-col cols="12" sm="6">
                <v-text-field
                  v-model="authConfig.entraRedirectUri"
                  label="Redirect URI"
                  placeholder="https://your-domain.com/auth/callback"
                  variant="outlined"
                  density="compact"
                />
              </v-col>
            </v-row>
          </v-card-text>
        </v-card>
      </v-col>

      <!-- Breakglass -->
      <v-col cols="12" lg="6">
        <v-card variant="outlined" class="fill-height">
          <v-card-title class="d-flex align-center py-3 bg-grey-darken-4">
            <v-icon icon="mdi-key-alert" class="mr-2" color="error" />
            Emergency Access
          </v-card-title>
          <v-card-text class="pt-4">
            <v-alert type="warning" variant="tonal" density="compact" class="mb-4">
              Breakglass password provides admin access when Entra ID is unavailable.
            </v-alert>
            <v-row dense>
              <v-col cols="12" sm="6">
                <v-text-field
                  v-model="authConfig.breakglassPassword"
                  label="New Breakglass Password"
                  :type="showBreakglass ? 'text' : 'password'"
                  :append-inner-icon="showBreakglass ? 'mdi-eye-off' : 'mdi-eye'"
                  @click:append-inner="showBreakglass = !showBreakglass"
                  variant="outlined"
                  density="compact"
                />
              </v-col>
              <v-col cols="12" sm="6">
                <v-text-field
                  v-model="authConfig.breakglassPasswordConfirm"
                  label="Confirm Password"
                  :type="showBreakglass ? 'text' : 'password'"
                  variant="outlined"
                  density="compact"
                  :error="authConfig.breakglassPassword !== authConfig.breakglassPasswordConfirm && authConfig.breakglassPasswordConfirm !== ''"
                  :error-messages="authConfig.breakglassPassword !== authConfig.breakglassPasswordConfirm && authConfig.breakglassPasswordConfirm !== '' ? 'Passwords do not match' : ''"
                />
              </v-col>
            </v-row>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>
  </v-container>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useCaptureStore } from '@/stores/capture'
import { useLocalPlaybackStore } from '@/stores/localPlayback'

const captureStore = useCaptureStore()
const localPlaybackStore = useLocalPlaybackStore()

// Local config copy
const config = ref({ ...captureStore.config })
const saving = ref(false)
const stats = ref({
  totalFiles: 0,
  totalSize: '0 GB'
})

// Network interfaces
const interfaces = ref([])

// Control interface network configuration
const networkConfig = ref({
  interface: null,
  mode: 'dhcp',
  ip_address: '',
  netmask: '255.255.255.0',
  gateway: '',
  dns_servers: [],
  bind_services: false
})
const networkSaving = ref(false)

// AES67 interface network configuration
const aes67Config = ref({
  interface: null,
  mode: 'dhcp',
  ip_address: '',
  netmask: '255.255.255.0',
  gateway: ''
})
const aes67Saving = ref(false)

// System configuration
const systemConfig = ref({
  hostname: '',
  timezone: 'UTC',
  ntp_servers: ['pool.ntp.org']
})
const timezones = ref([])

// SSL configuration
const sslConfig = ref({
  enabled: false,
  domain: '',
  email: '',
  cert_expiry: null,
  cert_type: 'none'
})
const sslLoading = ref(false)
const sslTab = ref('letsencrypt')

// Manual certificate upload
const manualCert = ref({
  domain: '',
  certFile: null,
  keyFile: null
})

// Authentication config
const authConfig = ref({
  entraTenantId: '',
  entraClientId: '',
  entraClientSecret: '',
  entraRedirectUri: '',
  breakglassPassword: '',
  breakglassPasswordConfirm: ''
})

// Password visibility toggles
const showClientSecret = ref(false)
const showBreakglass = ref(false)

// Log rotation config
const logRotation = ref({
  frequency: 'monthly',
  rotate_count: 12,
  compress: true,
  max_size: null
})
const logRotationSaving = ref(false)
const logRotationForcing = ref(false)

// Layout options
const layouts = [
  { title: 'Flat (YYYY-MM-DD-HH.ext)', value: 'flat' },
  { title: 'Hierarchy (YYYY/MM/DD/HH/archive.ext)', value: 'hierarchy' },
  { title: 'Combo (YYYY/MM/DD/HH/YYYY-MM-DD-HH.ext)', value: 'combo' },
  { title: 'Daily Directory (YYYY-MM-DD/...)', value: 'dailydir' },
  { title: 'Accurate (with seconds)', value: 'accurate' }
]

// Helper to format dates
function formatDate(dateStr) {
  if (!dateStr) return ''
  return new Date(dateStr).toLocaleDateString()
}

// Methods
async function saveSettings() {
  saving.value = true

  try {
    // Update the store
    captureStore.config = { ...config.value }

    // Save archive/PTP/AES67/VOX config to backend
    await fetch('/api/control/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        archive_root: config.value.archiveRoot,
        archive_layout: config.value.archiveLayout,
        archive_period: config.value.archivePeriod,
        archive_clock: config.value.archiveClock,
        ptp_interface: config.value.ptpInterface,
        aes67_interface: config.value.aes67Interface,
        vox_facility_enabled: config.value.voxFacilityEnabled
      })
    })

    // Save system config
    await fetch('/api/system/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        hostname: systemConfig.value.hostname || null,
        timezone: systemConfig.value.timezone,
        ntp_servers: systemConfig.value.ntp_servers
      })
    })

    // Save auth config if any fields are set
    if (authConfig.value.entraTenantId || authConfig.value.entraClientId ||
        authConfig.value.breakglassPassword) {
      // Validate breakglass password match
      if (authConfig.value.breakglassPassword &&
          authConfig.value.breakglassPassword !== authConfig.value.breakglassPasswordConfirm) {
        alert('Breakglass passwords do not match')
        return
      }

      await fetch('/auth/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          entra_tenant_id: authConfig.value.entraTenantId || undefined,
          entra_client_id: authConfig.value.entraClientId || undefined,
          entra_client_secret: authConfig.value.entraClientSecret || undefined,
          entra_redirect_uri: authConfig.value.entraRedirectUri || undefined,
          breakglass_password: authConfig.value.breakglassPassword || undefined
        })
      })

      // Clear password fields after save
      authConfig.value.entraClientSecret = ''
      authConfig.value.breakglassPassword = ''
      authConfig.value.breakglassPasswordConfirm = ''
    }
  } finally {
    saving.value = false
  }
}

function resetSettings() {
  config.value = {
    archiveRoot: '/var/lib/audyn',
    archiveLayout: 'dailydir',
    archivePeriod: 3600,
    archiveClock: 'localtime',
    ptpInterface: null,
    aes67Interface: null,
    voxFacilityEnabled: false
  }
  systemConfig.value = {
    hostname: '',
    timezone: 'UTC',
    ntp_servers: ['pool.ntp.org']
  }
}

async function fetchStats() {
  try {
    const response = await fetch('/api/assets/stats')
    if (response.ok) {
      const data = await response.json()
      stats.value = {
        totalFiles: data.total_files,
        totalSize: data.total_size_human || '0 GB'
      }
    }
  } catch (err) {
    console.error('Failed to fetch stats:', err)
  }
}

async function fetchAuthConfig() {
  try {
    const response = await fetch('/auth/config')
    if (response.ok) {
      const data = await response.json()
      authConfig.value.entraTenantId = data.entra_tenant_id || ''
      authConfig.value.entraClientId = data.entra_client_id || ''
      authConfig.value.entraRedirectUri = data.entra_redirect_uri || ''
      // Don't populate secrets - they should be re-entered
    }
  } catch (err) {
    console.error('Failed to fetch auth config:', err)
  }
}

async function fetchInterfaces() {
  try {
    const response = await fetch('/api/system/interfaces')
    if (response.ok) {
      const data = await response.json()
      interfaces.value = data.map(iface => ({
        name: iface.name,
        display: iface.ip_address ? `${iface.name} (${iface.ip_address})` : iface.name
      }))
    }
  } catch (err) {
    console.error('Failed to fetch interfaces:', err)
  }
}

async function fetchNetworkConfig() {
  try {
    const response = await fetch('/api/system/network')
    if (response.ok) {
      const data = await response.json()
      networkConfig.value.interface = data.interface || null
      networkConfig.value.bind_services = data.bind_services || false
      if (data.network) {
        networkConfig.value.mode = data.network.mode || 'dhcp'
        networkConfig.value.ip_address = data.network.ip_address || ''
        networkConfig.value.netmask = data.network.netmask || '255.255.255.0'
        networkConfig.value.gateway = data.network.gateway || ''
        networkConfig.value.dns_servers = data.network.dns_servers || []
      }
    }
  } catch (err) {
    console.error('Failed to fetch network config:', err)
  }
}

async function fetchAES67Config() {
  try {
    const response = await fetch('/api/system/aes67')
    if (response.ok) {
      const data = await response.json()
      aes67Config.value.interface = data.interface || null
      if (data.network) {
        aes67Config.value.mode = data.network.mode || 'dhcp'
        aes67Config.value.ip_address = data.network.ip_address || ''
        aes67Config.value.netmask = data.network.netmask || '255.255.255.0'
        aes67Config.value.gateway = data.network.gateway || ''
      }
    }
  } catch (err) {
    console.error('Failed to fetch AES67 config:', err)
  }
}

async function saveAES67Config() {
  if (!aes67Config.value.interface) {
    alert('Please select an AES67 interface')
    return
  }

  // Validate static IP settings
  if (aes67Config.value.mode === 'static') {
    if (!aes67Config.value.ip_address) {
      alert('IP address is required for static configuration')
      return
    }
  }

  aes67Saving.value = true
  try {
    const payload = {
      interface: aes67Config.value.interface,
      network: {
        interface: aes67Config.value.interface,
        mode: aes67Config.value.mode,
        ip_address: aes67Config.value.mode === 'static' ? aes67Config.value.ip_address : null,
        netmask: aes67Config.value.mode === 'static' ? aes67Config.value.netmask : null,
        gateway: aes67Config.value.mode === 'static' ? aes67Config.value.gateway : null,
        dns_servers: []
      }
    }

    const response = await fetch('/api/system/aes67', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    })

    if (response.ok) {
      alert('AES67 network configuration applied successfully!')
      // Also update the global config aes67Interface for the recorder
      config.value.aes67Interface = aes67Config.value.interface
    } else {
      const error = await response.json()
      alert(`Failed to apply AES67 network configuration: ${error.detail || 'Unknown error'}`)
    }
  } catch (err) {
    console.error('Failed to save AES67 config:', err)
    alert('Failed to apply AES67 network configuration. Check console for details.')
  } finally {
    aes67Saving.value = false
  }
}

async function saveNetworkConfig() {
  if (!networkConfig.value.interface) {
    alert('Please select a control interface')
    return
  }

  // Validate static IP settings
  if (networkConfig.value.mode === 'static') {
    if (!networkConfig.value.ip_address) {
      alert('IP address is required for static configuration')
      return
    }
  }

  networkSaving.value = true
  try {
    const payload = {
      interface: networkConfig.value.interface,
      bind_services: networkConfig.value.bind_services,
      network: {
        interface: networkConfig.value.interface,
        mode: networkConfig.value.mode,
        ip_address: networkConfig.value.mode === 'static' ? networkConfig.value.ip_address : null,
        netmask: networkConfig.value.mode === 'static' ? networkConfig.value.netmask : null,
        gateway: networkConfig.value.mode === 'static' ? networkConfig.value.gateway : null,
        dns_servers: networkConfig.value.mode === 'static' ? networkConfig.value.dns_servers : []
      }
    }

    const response = await fetch('/api/system/network', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    })

    if (response.ok) {
      const data = await response.json()
      alert('Network configuration applied successfully!')

      // If IP changed and bind_services is enabled, redirect to new IP
      if (networkConfig.value.mode === 'static' &&
          networkConfig.value.bind_services &&
          networkConfig.value.ip_address) {
        const protocol = window.location.protocol
        const newUrl = `${protocol}//${networkConfig.value.ip_address}`
        alert(`Redirecting to ${newUrl} in 5 seconds...`)
        setTimeout(() => {
          window.location.href = newUrl
        }, 5000)
      }
    } else {
      const error = await response.json()
      alert(`Failed to apply network configuration: ${error.detail || 'Unknown error'}`)
    }
  } catch (err) {
    console.error('Failed to save network config:', err)
    alert('Failed to apply network configuration. Check console for details.')
  } finally {
    networkSaving.value = false
  }
}

async function fetchTimezones() {
  try {
    const response = await fetch('/api/system/timezones')
    if (response.ok) {
      timezones.value = await response.json()
    }
  } catch (err) {
    console.error('Failed to fetch timezones:', err)
    // Provide some fallback timezones
    timezones.value = ['UTC', 'America/New_York', 'America/Los_Angeles', 'Europe/London', 'Europe/Paris']
  }
}

async function fetchSystemConfig() {
  try {
    const response = await fetch('/api/system/config')
    if (response.ok) {
      const data = await response.json()
      systemConfig.value.hostname = data.hostname || ''
      systemConfig.value.timezone = data.timezone || 'UTC'
      systemConfig.value.ntp_servers = data.ntp_servers || ['pool.ntp.org']
    }
  } catch (err) {
    console.error('Failed to fetch system config:', err)
  }
}

async function fetchSSLConfig() {
  try {
    const response = await fetch('/api/system/ssl')
    if (response.ok) {
      const data = await response.json()
      sslConfig.value.enabled = data.enabled || false
      sslConfig.value.domain = data.domain || ''
      sslConfig.value.email = data.email || ''
      sslConfig.value.cert_expiry = data.cert_expiry || null
      sslConfig.value.cert_type = data.cert_type || 'none'
    }
  } catch (err) {
    console.error('Failed to fetch SSL config:', err)
  }
}

async function uploadCertificate() {
  if (!manualCert.value.domain || !manualCert.value.certFile || !manualCert.value.keyFile) {
    alert('Domain, certificate file, and private key file are all required')
    return
  }

  sslLoading.value = true
  try {
    const formData = new FormData()
    formData.append('domain', manualCert.value.domain)
    formData.append('certificate', manualCert.value.certFile[0])
    formData.append('private_key', manualCert.value.keyFile[0])

    const response = await fetch('/api/system/ssl/upload', {
      method: 'POST',
      body: formData
    })

    if (response.ok) {
      const data = await response.json()
      sslConfig.value.enabled = true
      sslConfig.value.domain = manualCert.value.domain
      sslConfig.value.cert_type = 'manual'
      sslConfig.value.cert_expiry = data.config?.cert_expiry || null
      alert('SSL certificate installed successfully! The page will reload.')
      setTimeout(() => {
        window.location.href = `https://${manualCert.value.domain}`
      }, 2000)
    } else {
      const error = await response.json()
      alert(`Failed to install certificate: ${error.detail || 'Unknown error'}`)
    }
  } catch (err) {
    console.error('Failed to upload certificate:', err)
    alert('Failed to upload certificate. Check console for details.')
  } finally {
    sslLoading.value = false
  }
}

async function enableSSL() {
  if (!sslConfig.value.domain || !sslConfig.value.email) {
    alert('Domain and email are required for SSL')
    return
  }

  sslLoading.value = true
  try {
    const response = await fetch('/api/system/ssl/enable', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        domain: sslConfig.value.domain,
        email: sslConfig.value.email
      })
    })

    if (response.ok) {
      const data = await response.json()
      sslConfig.value.enabled = true
      sslConfig.value.expires_at = data.expires_at
      alert('SSL certificate enabled successfully! The page will reload.')
      // Reload to switch to HTTPS
      setTimeout(() => {
        window.location.href = `https://${sslConfig.value.domain}`
      }, 2000)
    } else {
      const error = await response.json()
      alert(`Failed to enable SSL: ${error.detail || 'Unknown error'}`)
    }
  } catch (err) {
    console.error('Failed to enable SSL:', err)
    alert('Failed to enable SSL. Check console for details.')
  } finally {
    sslLoading.value = false
  }
}

async function disableSSL() {
  if (!confirm('Are you sure you want to disable SSL? The site will switch to HTTP.')) {
    return
  }

  sslLoading.value = true
  try {
    const response = await fetch('/api/system/ssl/disable', {
      method: 'POST'
    })

    if (response.ok) {
      sslConfig.value.enabled = false
      sslConfig.value.expires_at = null
      alert('SSL disabled. The page will reload.')
      setTimeout(() => {
        window.location.href = `http://${window.location.hostname}`
      }, 2000)
    } else {
      const error = await response.json()
      alert(`Failed to disable SSL: ${error.detail || 'Unknown error'}`)
    }
  } catch (err) {
    console.error('Failed to disable SSL:', err)
    alert('Failed to disable SSL. Check console for details.')
  } finally {
    sslLoading.value = false
  }
}

async function fetchLogRotation() {
  try {
    const response = await fetch('/api/system/logrotate')
    if (response.ok) {
      const data = await response.json()
      logRotation.value = {
        frequency: data.frequency || 'monthly',
        rotate_count: data.rotate_count || 12,
        compress: data.compress !== false,
        max_size: data.max_size || null
      }
    }
  } catch (err) {
    console.error('Failed to fetch log rotation config:', err)
  }
}

async function saveLogRotation() {
  logRotationSaving.value = true
  try {
    const response = await fetch('/api/system/logrotate', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        frequency: logRotation.value.frequency,
        rotate_count: logRotation.value.rotate_count,
        compress: logRotation.value.compress,
        max_size: logRotation.value.max_size || null
      })
    })

    if (response.ok) {
      alert('Log rotation settings saved')
    } else {
      const error = await response.json()
      alert(`Failed to save log rotation: ${error.detail || 'Unknown error'}`)
    }
  } catch (err) {
    console.error('Failed to save log rotation:', err)
    alert('Failed to save log rotation. Check console for details.')
  } finally {
    logRotationSaving.value = false
  }
}

async function forceLogRotation() {
  if (!confirm('Force immediate log rotation?')) {
    return
  }

  logRotationForcing.value = true
  try {
    const response = await fetch('/api/system/logrotate/force', {
      method: 'POST'
    })

    if (response.ok) {
      alert('Log rotation completed successfully')
    } else {
      const error = await response.json()
      alert(`Failed to rotate logs: ${error.detail || 'Unknown error'}`)
    }
  } catch (err) {
    console.error('Failed to force log rotation:', err)
    alert('Failed to rotate logs. Check console for details.')
  } finally {
    logRotationForcing.value = false
  }
}

onMounted(async () => {
  // Fetch config from backend first, then copy to local state
  await captureStore.fetchConfig()
  config.value = { ...captureStore.config }

  // Load saved local playback folder (if any)
  localPlaybackStore.loadSavedFolder()

  // Fetch all configuration in parallel
  fetchStats()
  fetchAuthConfig()
  fetchInterfaces()
  fetchNetworkConfig()
  fetchAES67Config()
  fetchTimezones()
  fetchSystemConfig()
  fetchSSLConfig()
  fetchLogRotation()
})
</script>
