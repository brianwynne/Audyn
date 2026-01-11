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

// Methods
async function saveSettings() {
  saving.value = true

  try {
    // Update the store
    captureStore.config = { ...config.value }

    // Save archive/PTP config to backend
    await fetch('/api/control/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        archive_root: config.value.archiveRoot,
        archive_layout: config.value.archiveLayout,
        archive_period: config.value.archivePeriod,
        archive_clock: config.value.archiveClock,
        ptp_interface: config.value.ptpInterface
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
    ptpInterface: null
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

onMounted(async () => {
  // Fetch config from backend first, then copy to local state
  await captureStore.fetchConfig()
  config.value = { ...captureStore.config }
  fetchStats()
  fetchAuthConfig()
})
</script>
