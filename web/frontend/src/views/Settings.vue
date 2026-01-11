<template>
  <v-container fluid class="pa-6">
    <h1 class="text-h4 mb-6">Settings</h1>

    <v-row>
      <!-- Archive Settings -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>
            <v-icon icon="mdi-archive" class="mr-2" />
            Archive Settings
          </v-card-title>
          <v-card-text>
            <v-form>
              <v-text-field
                v-model="config.archiveRoot"
                label="Archive Directory"
                prepend-inner-icon="mdi-folder"
              />

              <v-select
                v-model="config.archiveLayout"
                label="Naming Layout"
                :items="layouts"
                item-title="title"
                item-value="value"
              />

              <v-text-field
                v-model.number="config.archivePeriod"
                label="Rotation Period"
                type="number"
                suffix="seconds"
                hint="3600 = 1 hour, 86400 = 24 hours"
                persistent-hint
              />

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
              />
            </v-form>
          </v-card-text>
        </v-card>
      </v-col>

      <!-- AES67 Network Settings -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>
            <v-icon icon="mdi-lan" class="mr-2" />
            AES67 Network
          </v-card-title>
          <v-card-text>
            <v-form>
              <v-select
                v-model="config.aes67Interface"
                label="AES67 Interface"
                :items="interfaces"
                item-title="display"
                item-value="name"
                hint="Network interface for multicast audio reception"
                persistent-hint
                clearable
              />
            </v-form>
          </v-card-text>
        </v-card>
      </v-col>

      <!-- PTP Settings -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>
            <v-icon icon="mdi-clock-outline" class="mr-2" />
            PTP Settings
          </v-card-title>
          <v-card-text>
            <v-form>
              <v-text-field
                v-model="config.ptpInterface"
                label="PTP Network Interface"
                placeholder="eth0"
                hint="Leave empty to disable PTP"
                persistent-hint
              />
            </v-form>
          </v-card-text>
        </v-card>
      </v-col>

      <!-- System Settings -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>
            <v-icon icon="mdi-cog" class="mr-2" />
            System Settings
          </v-card-title>
          <v-card-text>
            <v-form>
              <v-text-field
                v-model="systemConfig.hostname"
                label="Hostname"
                hint="System hostname (requires restart to apply)"
                persistent-hint
              />

              <v-autocomplete
                v-model="systemConfig.timezone"
                :items="timezones"
                label="Timezone"
                class="mt-4"
              />

              <v-combobox
                v-model="systemConfig.ntp_servers"
                label="NTP Servers"
                multiple
                chips
                closable-chips
                hint="Press Enter to add a server"
                persistent-hint
                class="mt-4"
              />
            </v-form>
          </v-card-text>
        </v-card>
      </v-col>

      <!-- SSL Certificate -->
      <v-col cols="12">
        <v-card>
          <v-card-title>
            <v-icon icon="mdi-lock" class="mr-2" />
            SSL Certificate
          </v-card-title>
          <v-card-text>
            <v-alert
              v-if="sslConfig.enabled"
              type="success"
              variant="tonal"
              class="mb-4"
            >
              SSL enabled for {{ sslConfig.domain }}
              <span v-if="sslConfig.cert_type">
                ({{ sslConfig.cert_type === 'letsencrypt' ? "Let's Encrypt" : 'Manual' }})
              </span>
              <span v-if="sslConfig.cert_expiry">
                - expires {{ formatDate(sslConfig.cert_expiry) }}
              </span>
            </v-alert>
            <v-alert
              v-else
              type="warning"
              variant="tonal"
              class="mb-4"
            >
              SSL not configured - using HTTP
            </v-alert>

            <v-tabs v-model="sslTab" class="mb-4">
              <v-tab value="letsencrypt">Let's Encrypt (Auto)</v-tab>
              <v-tab value="manual">Manual Upload</v-tab>
            </v-tabs>

            <v-window v-model="sslTab">
              <!-- Let's Encrypt Tab -->
              <v-window-item value="letsencrypt">
                <v-alert type="info" variant="tonal" density="compact" class="mb-4">
                  Requires port 80 to be accessible from the internet for domain verification.
                </v-alert>
                <v-form>
                  <v-row>
                    <v-col cols="12" md="6">
                      <v-text-field
                        v-model="sslConfig.domain"
                        label="Domain Name"
                        placeholder="audyn.example.com"
                        hint="Public domain name pointing to this server"
                        persistent-hint
                      />
                    </v-col>
                    <v-col cols="12" md="6">
                      <v-text-field
                        v-model="sslConfig.email"
                        label="Email for Let's Encrypt"
                        type="email"
                        placeholder="admin@example.com"
                        hint="Used for certificate expiry notifications"
                        persistent-hint
                      />
                    </v-col>
                  </v-row>

                  <v-btn
                    color="primary"
                    class="mt-4"
                    :loading="sslLoading"
                    :disabled="!sslConfig.domain || !sslConfig.email"
                    @click="enableSSL"
                  >
                    {{ sslConfig.enabled && sslConfig.cert_type === 'letsencrypt' ? 'Renew Certificate' : 'Enable HTTPS' }}
                  </v-btn>
                </v-form>
              </v-window-item>

              <!-- Manual Upload Tab -->
              <v-window-item value="manual">
                <v-alert type="info" variant="tonal" density="compact" class="mb-4">
                  Upload your own SSL certificate and private key (PEM format).
                </v-alert>
                <v-form>
                  <v-row>
                    <v-col cols="12" md="4">
                      <v-text-field
                        v-model="manualCert.domain"
                        label="Domain Name"
                        placeholder="audyn.example.com"
                        hint="Domain the certificate is issued for"
                        persistent-hint
                      />
                    </v-col>
                    <v-col cols="12" md="4">
                      <v-file-input
                        v-model="manualCert.certFile"
                        label="Certificate File"
                        accept=".crt,.pem,.cer"
                        prepend-icon="mdi-certificate"
                        hint=".crt, .pem, or .cer file"
                        persistent-hint
                      />
                    </v-col>
                    <v-col cols="12" md="4">
                      <v-file-input
                        v-model="manualCert.keyFile"
                        label="Private Key File"
                        accept=".key,.pem"
                        prepend-icon="mdi-key"
                        hint=".key or .pem file"
                        persistent-hint
                      />
                    </v-col>
                  </v-row>

                  <v-btn
                    color="primary"
                    class="mt-4"
                    :loading="sslLoading"
                    :disabled="!manualCert.domain || !manualCert.certFile || !manualCert.keyFile"
                    @click="uploadCertificate"
                  >
                    Upload & Install Certificate
                  </v-btn>
                </v-form>
              </v-window-item>
            </v-window>

            <v-divider class="my-4" v-if="sslConfig.enabled" />

            <v-btn
              v-if="sslConfig.enabled"
              color="error"
              variant="outlined"
              :loading="sslLoading"
              @click="disableSSL"
            >
              Disable SSL
            </v-btn>
          </v-card-text>
        </v-card>
      </v-col>

      <!-- Authentication Settings -->
      <v-col cols="12">
        <v-card>
          <v-card-title>
            <v-icon icon="mdi-shield-account" class="mr-2" />
            Authentication Settings
          </v-card-title>
          <v-card-text>
            <v-row>
              <!-- Entra ID Configuration -->
              <v-col cols="12">
                <div class="text-subtitle-2 text-medium-emphasis mb-2">Microsoft Entra ID (Azure AD)</div>
              </v-col>
              <v-col cols="12" md="6">
                <v-text-field
                  v-model="authConfig.entraTenantId"
                  label="Tenant ID"
                  placeholder="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
                  variant="outlined"
                  density="compact"
                />
              </v-col>
              <v-col cols="12" md="6">
                <v-text-field
                  v-model="authConfig.entraClientId"
                  label="Client ID (Application ID)"
                  placeholder="xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
                  variant="outlined"
                  density="compact"
                />
              </v-col>
              <v-col cols="12" md="6">
                <v-text-field
                  v-model="authConfig.entraClientSecret"
                  label="Client Secret"
                  :type="showClientSecret ? 'text' : 'password'"
                  :append-inner-icon="showClientSecret ? 'mdi-eye-off' : 'mdi-eye'"
                  @click:append-inner="showClientSecret = !showClientSecret"
                  variant="outlined"
                  density="compact"
                  hint="Leave empty to keep existing secret"
                  persistent-hint
                />
              </v-col>
              <v-col cols="12" md="6">
                <v-text-field
                  v-model="authConfig.entraRedirectUri"
                  label="Redirect URI"
                  placeholder="https://your-domain.com/auth/callback"
                  variant="outlined"
                  density="compact"
                />
              </v-col>

              <!-- Breakglass Password -->
              <v-col cols="12">
                <v-divider class="my-4" />
                <div class="text-subtitle-2 text-medium-emphasis mb-2">Emergency Access</div>
              </v-col>
              <v-col cols="12" md="6">
                <v-text-field
                  v-model="authConfig.breakglassPassword"
                  label="Breakglass Password"
                  :type="showBreakglass ? 'text' : 'password'"
                  :append-inner-icon="showBreakglass ? 'mdi-eye-off' : 'mdi-eye'"
                  @click:append-inner="showBreakglass = !showBreakglass"
                  variant="outlined"
                  density="compact"
                  hint="Master password for emergency admin access when Entra ID is unavailable"
                  persistent-hint
                />
              </v-col>
              <v-col cols="12" md="6">
                <v-text-field
                  v-model="authConfig.breakglassPasswordConfirm"
                  label="Confirm Breakglass Password"
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

      <!-- System Info -->
      <v-col cols="12" md="6">
        <v-card>
          <v-card-title>
            <v-icon icon="mdi-information" class="mr-2" />
            System Information
          </v-card-title>
          <v-card-text>
            <v-list density="compact">
              <v-list-item>
                <v-list-item-title>Version</v-list-item-title>
                <v-list-item-subtitle>Audyn v1.0.0</v-list-item-subtitle>
              </v-list-item>
              <v-list-item>
                <v-list-item-title>Backend Status</v-list-item-title>
                <v-list-item-subtitle>
                  <v-chip color="success" size="small">Connected</v-chip>
                </v-list-item-subtitle>
              </v-list-item>
              <v-list-item>
                <v-list-item-title>Archive Stats</v-list-item-title>
                <v-list-item-subtitle>
                  {{ stats.totalFiles }} files, {{ stats.totalSize }}
                </v-list-item-subtitle>
              </v-list-item>
            </v-list>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>

    <!-- Save Button -->
    <v-row class="mt-4">
      <v-col>
        <v-btn
          color="primary"
          size="large"
          prepend-icon="mdi-content-save"
          :loading="saving"
          @click="saveSettings"
        >
          Save Settings
        </v-btn>

        <v-btn
          variant="outlined"
          size="large"
          class="ml-4"
          @click="resetSettings"
        >
          Reset to Defaults
        </v-btn>
      </v-col>
    </v-row>
  </v-container>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useCaptureStore } from '@/stores/capture'

const captureStore = useCaptureStore()

// Local config copy
const config = ref({ ...captureStore.config })
const saving = ref(false)
const stats = ref({
  totalFiles: 0,
  totalSize: '0 GB'
})

// Network interfaces
const interfaces = ref([])

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

    // Save archive/PTP/AES67 config to backend
    await fetch('/api/control/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        archive_root: config.value.archiveRoot,
        archive_layout: config.value.archiveLayout,
        archive_period: config.value.archivePeriod,
        archive_clock: config.value.archiveClock,
        ptp_interface: config.value.ptpInterface,
        aes67_interface: config.value.aes67Interface
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
    aes67Interface: null
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

onMounted(async () => {
  // Fetch config from backend first, then copy to local state
  await captureStore.fetchConfig()
  config.value = { ...captureStore.config }

  // Fetch all configuration in parallel
  fetchStats()
  fetchAuthConfig()
  fetchInterfaces()
  fetchTimezones()
  fetchSystemConfig()
  fetchSSLConfig()
})
</script>
