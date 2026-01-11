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

      <!-- Current Studio Indicator (for non-admins) -->
      <div v-if="!authStore.isAdmin && currentStudio" class="px-4 py-2">
        <v-chip
          :color="currentStudio.color"
          variant="flat"
          size="small"
          block
          class="justify-center"
        >
          <v-icon start size="small">mdi-broadcast</v-icon>
          {{ currentStudio.name }}
        </v-chip>
      </div>

      <v-divider />

      <v-list density="compact" nav>
        <!-- Regular user items -->
        <template v-if="!authStore.isAdmin">
          <v-list-item
            v-if="authStore.hasSelectedStudio"
            prepend-icon="mdi-view-dashboard"
            title="My Studio"
            value="studio-view"
            :to="{ name: 'studio-view', params: { id: authStore.selectedStudioId } }"
          />
          <v-list-item
            prepend-icon="mdi-swap-horizontal"
            title="Switch Studio"
            value="studio-select"
            :to="{ name: 'studio-select' }"
          />
        </template>

        <!-- Admin items -->
        <template v-if="authStore.isAdmin">
          <v-list-item
            prepend-icon="mdi-broadcast"
            title="Studios"
            value="studio-select"
            :to="{ name: 'studio-select' }"
          />

          <v-divider class="my-2" />
          <v-list-subheader>Admin</v-list-subheader>

          <v-list-item
            prepend-icon="mdi-folder-music"
            title="All Files"
            value="files"
            :to="{ name: 'files' }"
          />
          <v-list-item
            prepend-icon="mdi-view-dashboard"
            title="All Recorders"
            value="overview"
            :to="{ name: 'overview' }"
          />
          <v-list-item
            prepend-icon="mdi-record-circle"
            title="Recorder Config"
            value="recorders"
            :to="{ name: 'recorders' }"
          />
          <v-list-item
            prepend-icon="mdi-radio-tower"
            title="Studio Config"
            value="studios"
            :to="{ name: 'studios' }"
          />
          <v-list-item
            prepend-icon="mdi-access-point-network"
            title="AES67 Sources"
            value="sources"
            :to="{ name: 'sources' }"
          />
          <v-list-item
            prepend-icon="mdi-cog"
            title="Settings"
            value="settings"
            :to="{ name: 'settings' }"
          />
        </template>
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
        <!-- Recording Status -->
        <v-chip
          :color="recordingCount > 0 ? 'error' : 'default'"
          :prepend-icon="recordingCount > 0 ? 'mdi-record-circle' : 'mdi-stop-circle'"
          class="mr-4"
        >
          <template v-if="recordingCount > 0">
            {{ recordingCount }} REC
          </template>
          <template v-else>
            STOPPED
          </template>
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
import { useRecordersStore } from '@/stores/recorders'
import { useStudiosStore } from '@/stores/studios'

const router = useRouter()
const theme = useTheme()
const authStore = useAuthStore()
const captureStore = useCaptureStore()
const recordersStore = useRecordersStore()
const studiosStore = useStudiosStore()

// Recording count across all recorders
const recordingCount = computed(() =>
  recordersStore.recordingRecorders.length
)

// Current studio for non-admin users
const currentStudio = computed(() => {
  if (authStore.selectedStudioId) {
    return studiosStore.getStudioById(authStore.selectedStudioId)
  }
  return null
})

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
  recordersStore.disconnectLevels()
  await authStore.logout()
  router.push({ name: 'login' })
}

// Initialize on mount
onMounted(async () => {
  if (authStore.isAuthenticated) {
    await captureStore.fetchStatus()
    await captureStore.fetchSources()
    await recordersStore.fetchRecorders()
    await studiosStore.fetchStudios()
    recordersStore.connectLevels()

    // Fetch user's selected studio
    await authStore.fetchSelectedStudio()
  }
})

// Watch for authentication changes
watch(() => authStore.isAuthenticated, async (isAuth) => {
  if (isAuth) {
    await captureStore.fetchStatus()
    await captureStore.fetchSources()
    await recordersStore.fetchRecorders()
    await studiosStore.fetchStudios()
    recordersStore.connectLevels()
    await authStore.fetchSelectedStudio()
  } else {
    recordersStore.disconnectLevels()
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
