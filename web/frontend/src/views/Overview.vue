<template>
  <v-container fluid class="pa-4">
    <!-- Header -->
    <div class="d-flex align-center mb-4">
      <h1 class="text-h4">Overview</h1>
      <v-spacer />

      <!-- Recording controls (admin only) -->
      <template v-if="authStore.isAdmin">
        <v-btn
          color="success"
          variant="elevated"
          prepend-icon="mdi-play"
          class="mr-2"
          :loading="recordersStore.loading"
          @click="startAll"
        >
          Start All
        </v-btn>
        <v-btn
          color="error"
          variant="elevated"
          prepend-icon="mdi-stop"
          :loading="recordersStore.loading"
          @click="stopAll"
        >
          Stop All
        </v-btn>
      </template>
    </div>

    <!-- Connection status -->
    <v-alert
      v-if="!recordersStore.connected"
      type="warning"
      variant="tonal"
      class="mb-4"
    >
      <v-icon>mdi-connection</v-icon>
      Connecting to live audio meters...
    </v-alert>

    <!-- Recorder grid -->
    <v-row>
      <v-col
        v-for="recorder in recordersStore.activeRecorders"
        :key="recorder.id"
        cols="12"
        md="6"
        lg="4"
      >
        <RecorderCard :recorder="recorder" :studio="getStudioForRecorder(recorder)" />
      </v-col>
    </v-row>

    <!-- Empty state -->
    <v-alert
      v-if="recordersStore.activeRecorders.length === 0"
      type="info"
      variant="tonal"
      class="mt-4"
    >
      No active recorders configured.
    </v-alert>

    <!-- Summary stats -->
    <v-row class="mt-4">
      <v-col cols="12" md="4">
        <v-card variant="tonal" color="primary">
          <v-card-text class="text-center">
            <div class="text-h3">{{ recordersStore.activeRecorders.length }}</div>
            <div class="text-subtitle-1">Active Recorders</div>
          </v-card-text>
        </v-card>
      </v-col>
      <v-col cols="12" md="4">
        <v-card variant="tonal" color="success">
          <v-card-text class="text-center">
            <div class="text-h3">{{ recordersStore.recordingRecorders.length }}</div>
            <div class="text-subtitle-1">Currently Recording</div>
          </v-card-text>
        </v-card>
      </v-col>
      <v-col cols="12" md="4">
        <v-card variant="tonal" color="secondary">
          <v-card-text class="text-center">
            <div class="text-h3">{{ studiosStore.studiosWithRecorders.length }}</div>
            <div class="text-subtitle-1">Studios Assigned</div>
          </v-card-text>
        </v-card>
      </v-col>
    </v-row>
  </v-container>
</template>

<script setup>
import { onMounted, onUnmounted } from 'vue'
import { useAuthStore } from '@/stores/auth'
import { useRecordersStore } from '@/stores/recorders'
import { useStudiosStore } from '@/stores/studios'
import RecorderCard from '@/components/RecorderCard.vue'

const authStore = useAuthStore()
const recordersStore = useRecordersStore()
const studiosStore = useStudiosStore()

function getStudioForRecorder(recorder) {
  if (!recorder.studio_id) return null
  return studiosStore.getStudioById(recorder.studio_id)
}

async function startAll() {
  await recordersStore.startAllRecorders()
}

async function stopAll() {
  await recordersStore.stopAllRecorders()
}

onMounted(async () => {
  await Promise.all([
    recordersStore.fetchRecorders(),
    recordersStore.fetchActiveCount(),
    studiosStore.fetchStudios()
  ])
  recordersStore.connectLevels()
})

onUnmounted(() => {
  recordersStore.disconnectLevels()
})
</script>
