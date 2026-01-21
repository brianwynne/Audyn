<template>
  <v-container fluid class="pa-6">
    <div class="d-flex justify-space-between align-center mb-6">
      <h1 class="text-h4">AES67 Sources</h1>
      <div>
        <v-btn
          color="secondary"
          variant="tonal"
          prepend-icon="mdi-access-point-network"
          class="mr-2"
          @click="showBrowser = true"
        >
          Discover Streams
        </v-btn>
        <v-btn color="primary" prepend-icon="mdi-plus" @click="openAddDialog">
          Add Source
        </v-btn>
      </div>
    </div>

    <!-- Stream Browser Dialog -->
    <StreamBrowser v-model="showBrowser" @imported="onStreamImported" />

    <v-row>
      <v-col
        v-for="source in captureStore.sources"
        :key="source.id"
        cols="12"
        sm="6"
        md="4"
      >
        <v-card
          :color="source.id === captureStore.activeSourceId ? 'primary' : undefined"
          :variant="source.id === captureStore.activeSourceId ? 'tonal' : 'elevated'"
        >
          <v-card-title class="d-flex align-center">
            <v-icon
              :icon="source.enabled ? 'mdi-access-point' : 'mdi-access-point-off'"
              class="mr-2"
            />
            {{ source.name }}
            <v-chip
              v-if="source.id === captureStore.activeSourceId"
              color="success"
              size="small"
              class="ml-2"
            >
              Active
            </v-chip>
          </v-card-title>

          <v-card-text>
            <v-list density="compact" class="bg-transparent">
              <v-list-item>
                <template v-slot:prepend>
                  <v-icon icon="mdi-ip-network" size="small" />
                </template>
                <v-list-item-title>{{ source.multicast_addr }}:{{ source.port }}</v-list-item-title>
              </v-list-item>
              <v-list-item>
                <template v-slot:prepend>
                  <v-icon icon="mdi-waveform" size="small" />
                </template>
                <v-list-item-title>{{ source.sample_rate }} Hz / {{ source.channels }} ch</v-list-item-title>
              </v-list-item>
              <v-list-item v-if="source.description">
                <template v-slot:prepend>
                  <v-icon icon="mdi-information" size="small" />
                </template>
                <v-list-item-title>{{ source.description }}</v-list-item-title>
              </v-list-item>
            </v-list>
          </v-card-text>

          <v-card-actions>
            <v-btn
              v-if="source.id !== captureStore.activeSourceId"
              color="primary"
              variant="text"
              @click="selectSource(source)"
            >
              Select
            </v-btn>
            <v-spacer />
            <v-btn icon size="small" @click="editSource(source)">
              <v-icon>mdi-pencil</v-icon>
            </v-btn>
            <v-btn
              icon
              size="small"
              :disabled="source.id === captureStore.activeSourceId"
              @click="deleteSource(source)"
            >
              <v-icon>mdi-delete</v-icon>
            </v-btn>
          </v-card-actions>
        </v-card>
      </v-col>
    </v-row>

    <!-- Add/Edit Dialog -->
    <v-dialog v-model="dialog.show" max-width="500">
      <v-card>
        <v-card-title>
          {{ dialog.isEdit ? 'Edit Source' : 'Add Source' }}
        </v-card-title>
        <v-card-text>
          <v-form ref="form" v-model="dialog.valid">
            <v-text-field
              v-model="dialog.data.name"
              label="Name"
              :rules="[v => !!v || 'Name is required']"
              required
            />
            <v-text-field
              v-model="dialog.data.multicast_addr"
              label="Multicast Address"
              placeholder="239.69.1.1"
              :rules="[v => !!v || 'Address is required']"
              required
            />
            <v-text-field
              v-model.number="dialog.data.port"
              label="Port"
              type="number"
              :rules="[v => v > 0 || 'Invalid port']"
            />
            <v-row>
              <v-col cols="6">
                <v-select
                  v-model="dialog.data.sample_rate"
                  label="Sample Rate"
                  :items="[44100, 48000, 96000]"
                />
              </v-col>
              <v-col cols="6">
                <v-select
                  v-model="dialog.data.channels"
                  label="Channels"
                  :items="[1, 2, 4, 6, 8]"
                />
              </v-col>
            </v-row>
            <v-text-field
              v-model="dialog.data.description"
              label="Description"
            />
          </v-form>
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="dialog.show = false">Cancel</v-btn>
          <v-btn
            color="primary"
            :disabled="!dialog.valid"
            :loading="dialog.loading"
            @click="saveSource"
          >
            Save
          </v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Delete Confirmation -->
    <v-dialog v-model="deleteDialog.show" max-width="400">
      <v-card>
        <v-card-title>Delete Source</v-card-title>
        <v-card-text>
          Are you sure you want to delete "{{ deleteDialog.source?.name }}"?
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="deleteDialog.show = false">Cancel</v-btn>
          <v-btn color="error" @click="confirmDelete">Delete</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>
  </v-container>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useCaptureStore } from '@/stores/capture'
import StreamBrowser from '@/components/StreamBrowser.vue'

const captureStore = useCaptureStore()
const showBrowser = ref(false)

// Dialog state
const dialog = ref({
  show: false,
  isEdit: false,
  valid: false,
  loading: false,
  data: {
    id: null,
    name: '',
    multicast_addr: '',
    port: 5004,
    sample_rate: 48000,
    channels: 2,
    description: ''
  }
})

const deleteDialog = ref({
  show: false,
  source: null
})

// Methods
function openAddDialog() {
  dialog.value = {
    show: true,
    isEdit: false,
    valid: false,
    loading: false,
    data: {
      id: null,
      name: '',
      multicast_addr: '',
      port: 5004,
      sample_rate: 48000,
      channels: 2,
      description: ''
    }
  }
}

function editSource(source) {
  dialog.value = {
    show: true,
    isEdit: true,
    valid: true,
    loading: false,
    data: { ...source }
  }
}

async function saveSource() {
  dialog.value.loading = true

  try {
    const method = dialog.value.isEdit ? 'PUT' : 'POST'
    const url = dialog.value.isEdit
      ? `/api/sources/${dialog.value.data.id}`
      : '/api/sources/'

    const response = await fetch(url, {
      method,
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(dialog.value.data)
    })

    if (response.ok) {
      await captureStore.fetchSources()
      dialog.value.show = false
    }
  } catch (err) {
    console.error('Failed to save source:', err)
  } finally {
    dialog.value.loading = false
  }
}

function deleteSource(source) {
  deleteDialog.value = {
    show: true,
    source
  }
}

async function confirmDelete() {
  try {
    const response = await fetch(`/api/sources/${deleteDialog.value.source.id}`, {
      method: 'DELETE'
    })

    if (response.ok) {
      await captureStore.fetchSources()
    }
  } catch (err) {
    console.error('Failed to delete source:', err)
  } finally {
    deleteDialog.value.show = false
  }
}

async function selectSource(source) {
  await captureStore.switchSource(source.id)
}

async function onStreamImported(newSource) {
  // Refresh sources list after importing a stream
  await captureStore.fetchSources()
  showBrowser.value = false
}

onMounted(() => {
  captureStore.fetchSources()
})
</script>
