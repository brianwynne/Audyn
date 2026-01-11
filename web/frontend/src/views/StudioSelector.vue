<template>
  <v-container fluid class="pa-6 fill-height">
    <v-row align="center" justify="center">
      <v-col cols="12" md="10" lg="8">
        <!-- Header -->
        <div class="text-center mb-8">
          <v-icon icon="mdi-radio-tower" size="64" color="primary" class="mb-4" />
          <h1 class="text-h3 mb-2">Select Your Studio</h1>
          <p class="text-subtitle-1 text-medium-emphasis">
            Choose a studio to view its recorders and audio files
          </p>
        </div>

        <!-- Loading State -->
        <v-progress-circular
          v-if="loading"
          indeterminate
          color="primary"
          size="64"
          class="d-block mx-auto mb-8"
        />

        <!-- Studios Grid -->
        <v-row v-else>
          <v-col
            v-for="studio in studios"
            :key="studio.id"
            cols="12"
            sm="6"
            md="4"
          >
            <v-card
              hover
              class="studio-card"
              :style="{ borderLeftColor: studio.color, borderLeftWidth: '4px', borderLeftStyle: 'solid' }"
              @click="selectStudio(studio)"
            >
              <v-card-item>
                <template v-slot:prepend>
                  <v-avatar :color="studio.color" size="48">
                    <v-icon icon="mdi-broadcast" size="24" />
                  </v-avatar>
                </template>
                <v-card-title>{{ studio.name }}</v-card-title>
                <v-card-subtitle v-if="studio.description">
                  {{ studio.description }}
                </v-card-subtitle>
              </v-card-item>

              <v-card-text>
                <div class="d-flex align-center justify-space-between">
                  <div>
                    <v-chip
                      v-if="studio.recorder_id"
                      size="small"
                      color="success"
                      variant="tonal"
                      prepend-icon="mdi-record-circle"
                    >
                      Recorder {{ studio.recorder_id }}
                    </v-chip>
                    <v-chip
                      v-else
                      size="small"
                      color="grey"
                      variant="tonal"
                    >
                      No Recorder
                    </v-chip>
                  </div>
                  <v-icon icon="mdi-chevron-right" color="primary" />
                </div>
              </v-card-text>
            </v-card>
          </v-col>
        </v-row>

        <!-- Admin Dashboard Option -->
        <v-divider v-if="authStore.isAdmin" class="my-8" />

        <v-row v-if="authStore.isAdmin" justify="center">
          <v-col cols="12" sm="6" md="4">
            <v-card
              hover
              variant="outlined"
              class="text-center pa-4"
              @click="goToAdminDashboard"
            >
              <v-icon icon="mdi-shield-crown" size="48" color="primary" class="mb-2" />
              <v-card-title class="text-h6">Admin Dashboard</v-card-title>
              <v-card-subtitle>
                Manage all recorders, studios, and settings
              </v-card-subtitle>
            </v-card>
          </v-col>
        </v-row>

        <!-- Empty State -->
        <v-alert
          v-if="!loading && studios.length === 0"
          type="info"
          variant="tonal"
          class="mt-4"
        >
          <v-alert-title>No Studios Available</v-alert-title>
          No studios have been configured yet. Please contact an administrator.
        </v-alert>
      </v-col>
    </v-row>
  </v-container>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useAuthStore } from '@/stores/auth'
import { useStudiosStore } from '@/stores/studios'

const router = useRouter()
const authStore = useAuthStore()
const studiosStore = useStudiosStore()

const loading = ref(true)

const studios = computed(() => studiosStore.studios)

async function selectStudio(studio) {
  try {
    // Call backend to set selection
    const response = await fetch(`/api/studios/select/${studio.id}`, {
      method: 'POST'
    })

    if (response.ok) {
      // Update local state
      authStore.setSelectedStudio(studio.id)
      // Navigate to studio view
      router.push({ name: 'studio-view', params: { id: studio.id } })
    }
  } catch (err) {
    console.error('Failed to select studio:', err)
  }
}

function goToAdminDashboard() {
  router.push({ name: 'overview' })
}

onMounted(async () => {
  loading.value = true
  await studiosStore.fetchStudios()
  loading.value = false
})
</script>

<style scoped>
.studio-card {
  cursor: pointer;
  transition: transform 0.2s, box-shadow 0.2s;
}

.studio-card:hover {
  transform: translateY(-4px);
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.15);
}
</style>
