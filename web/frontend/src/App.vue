<template>
  <v-app>
    <!-- Navigation Drawer -->
    <v-navigation-drawer
      v-if="authStore.isAuthenticated"
      v-model="drawer"
      :rail="rail"
      permanent
    >
      <v-list-item
        :prepend-avatar="undefined"
        :title="authStore.userName"
        :subtitle="authStore.userEmail"
        nav
      >
        <template v-slot:prepend>
          <v-avatar color="primary">
            <span class="text-h6">{{ initials }}</span>
          </v-avatar>
        </template>
        <template v-slot:append>
          <v-btn
            variant="text"
            :icon="rail ? 'mdi-chevron-right' : 'mdi-chevron-left'"
            @click.stop="rail = !rail"
          />
        </template>
      </v-list-item>

      <v-divider />

      <v-list density="compact" nav>
        <v-list-item
          prepend-icon="mdi-view-dashboard"
          title="Dashboard"
          value="dashboard"
          :to="{ name: 'dashboard' }"
        />
        <v-list-item
          prepend-icon="mdi-access-point-network"
          title="Sources"
          value="sources"
          :to="{ name: 'sources' }"
        />
        <v-list-item
          prepend-icon="mdi-folder-music"
          title="Files"
          value="files"
          :to="{ name: 'files' }"
        />
        <v-list-item
          prepend-icon="mdi-cog"
          title="Settings"
          value="settings"
          :to="{ name: 'settings' }"
        />
      </v-list>

      <template v-slot:append>
        <div class="pa-2">
          <v-btn
            block
            variant="outlined"
            prepend-icon="mdi-logout"
            @click="handleLogout"
          >
            {{ rail ? '' : 'Logout' }}
          </v-btn>
        </div>
      </template>
    </v-navigation-drawer>

    <!-- App Bar -->
    <v-app-bar v-if="authStore.isAuthenticated" elevation="1">
      <v-app-bar-title>
        <v-icon icon="mdi-radio-tower" class="mr-2" />
        AUDYN
      </v-app-bar-title>

      <template v-slot:append>
        <!-- Source Selector -->
        <v-select
          v-if="captureStore.sources.length"
          v-model="captureStore.activeSourceId"
          :items="captureStore.sources"
          item-title="name"
          item-value="id"
          label="Source"
          density="compact"
          variant="outlined"
          hide-details
          style="max-width: 200px"
          class="mr-4"
          @update:model-value="captureStore.switchSource($event)"
        />

        <!-- Recording Status -->
        <v-chip
          :color="captureStore.isRecording ? 'recording' : 'default'"
          :prepend-icon="captureStore.isRecording ? 'mdi-record-circle' : 'mdi-stop-circle'"
          class="mr-4"
        >
          {{ captureStore.isRecording ? 'RECORDING' : 'STOPPED' }}
        </v-chip>

        <!-- Theme Toggle -->
        <v-btn
          icon
          @click="toggleTheme"
        >
          <v-icon>{{ isDark ? 'mdi-weather-sunny' : 'mdi-weather-night' }}</v-icon>
        </v-btn>
      </template>
    </v-app-bar>

    <!-- Main Content -->
    <v-main>
      <router-view />
    </v-main>

    <!-- Snackbar for notifications -->
    <v-snackbar
      v-model="snackbar.show"
      :color="snackbar.color"
      :timeout="snackbar.timeout"
    >
      {{ snackbar.text }}
      <template v-slot:actions>
        <v-btn variant="text" @click="snackbar.show = false">Close</v-btn>
      </template>
    </v-snackbar>
  </v-app>
</template>

<script setup>
import { ref, computed, onMounted, watch } from 'vue'
import { useTheme } from 'vuetify'
import { useRouter } from 'vue-router'
import { useAuthStore } from '@/stores/auth'
import { useCaptureStore } from '@/stores/capture'

const router = useRouter()
const theme = useTheme()
const authStore = useAuthStore()
const captureStore = useCaptureStore()

// UI State
const drawer = ref(true)
const rail = ref(false)
const snackbar = ref({
  show: false,
  text: '',
  color: 'info',
  timeout: 3000
})

// Theme
const isDark = computed(() => theme.global.current.value.dark)

function toggleTheme() {
  theme.global.name.value = isDark.value ? 'audynLightTheme' : 'audynTheme'
}

// User initials for avatar
const initials = computed(() => {
  const name = authStore.userName
  return name
    .split(' ')
    .map(n => n[0])
    .join('')
    .toUpperCase()
    .slice(0, 2)
})

// Logout handler
async function handleLogout() {
  captureStore.disconnectLevels()
  await authStore.logout()
  router.push({ name: 'login' })
}

// Initialize on mount
onMounted(async () => {
  if (authStore.isAuthenticated) {
    await captureStore.fetchStatus()
    await captureStore.fetchSources()
    captureStore.connectLevels()
  }
})

// Watch for authentication changes
watch(() => authStore.isAuthenticated, async (isAuth) => {
  if (isAuth) {
    await captureStore.fetchStatus()
    await captureStore.fetchSources()
    captureStore.connectLevels()
  } else {
    captureStore.disconnectLevels()
  }
})

// Watch for errors
watch(() => captureStore.error, (err) => {
  if (err) {
    snackbar.value = {
      show: true,
      text: err,
      color: 'error',
      timeout: 5000
    }
  }
})
</script>

<style>
.v-application {
  font-family: 'Roboto', sans-serif;
}
</style>
