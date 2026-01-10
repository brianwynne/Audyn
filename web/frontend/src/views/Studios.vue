<template>
  <v-container fluid class="pa-4">
    <!-- Header -->
    <div class="d-flex align-center mb-4">
      <h1 class="text-h4">Studios</h1>
      <v-spacer />
      <v-btn
        color="primary"
        variant="elevated"
        prepend-icon="mdi-plus"
        @click="openCreateDialog"
      >
        Add Studio
      </v-btn>
    </div>

    <!-- Studios grid -->
    <v-row>
      <v-col
        v-for="studio in studiosStore.studios"
        :key="studio.id"
        cols="12"
        md="6"
        lg="4"
      >
        <v-card :style="{ borderLeft: `4px solid ${studio.color}` }">
          <v-card-title class="d-flex align-center">
            <v-chip :color="studio.color" size="small" class="mr-2" />
            {{ studio.name }}
            <v-spacer />
            <v-chip
              v-if="!studio.enabled"
              color="warning"
              size="x-small"
              variant="outlined"
            >
              Disabled
            </v-chip>
          </v-card-title>

          <v-card-subtitle v-if="studio.description">
            {{ studio.description }}
          </v-card-subtitle>

          <v-card-text>
            <!-- Assigned recorder info -->
            <div class="mb-3">
              <div class="text-subtitle-2 mb-1">Assigned Recorder</div>
              <template v-if="getRecorder(studio.recorder_id)">
                <v-chip
                  :color="getRecorderStateColor(getRecorder(studio.recorder_id))"
                  variant="tonal"
                >
                  <v-icon start :icon="getRecorderStateIcon(getRecorder(studio.recorder_id))" size="small" />
                  {{ getRecorder(studio.recorder_id).name }}
                </v-chip>
              </template>
              <span v-else class="text-medium-emphasis">None</span>
            </div>

            <!-- Recorder assignment -->
            <v-select
              :model-value="studio.recorder_id"
              :items="availableRecorderOptions(studio)"
              label="Assign Recorder"
              variant="outlined"
              density="compact"
              clearable
              hide-details
              @update:model-value="assignRecorder(studio.id, $event)"
            />
          </v-card-text>

          <v-card-actions>
            <v-btn
              variant="text"
              prepend-icon="mdi-pencil"
              @click="openEditDialog(studio)"
            >
              Edit
            </v-btn>
            <v-spacer />
            <v-btn
              variant="text"
              color="error"
              prepend-icon="mdi-delete"
              @click="confirmDelete(studio)"
            >
              Delete
            </v-btn>
          </v-card-actions>
        </v-card>
      </v-col>
    </v-row>

    <!-- Empty state -->
    <v-alert
      v-if="studiosStore.studios.length === 0"
      type="info"
      variant="tonal"
    >
      No studios configured. Click "Add Studio" to create one.
    </v-alert>

    <!-- Create/Edit Dialog -->
    <v-dialog v-model="dialog" max-width="500">
      <v-card>
        <v-card-title>
          {{ editingStudio ? 'Edit Studio' : 'Create Studio' }}
        </v-card-title>
        <v-card-text>
          <v-form ref="formRef" @submit.prevent="saveStudio">
            <v-text-field
              v-model="form.name"
              label="Studio Name"
              variant="outlined"
              :rules="[v => !!v || 'Name is required']"
              class="mb-3"
            />
            <v-text-field
              v-model="form.description"
              label="Description"
              variant="outlined"
              class="mb-3"
            />
            <div class="d-flex align-center mb-3">
              <span class="mr-3">Color:</span>
              <v-btn
                v-for="color in colors"
                :key="color"
                icon
                size="small"
                :color="color"
                :variant="form.color === color ? 'elevated' : 'flat'"
                class="mx-1"
                @click="form.color = color"
              >
                <v-icon v-if="form.color === color">mdi-check</v-icon>
              </v-btn>
            </div>
            <v-switch
              v-if="editingStudio"
              v-model="form.enabled"
              label="Enabled"
              color="primary"
            />
          </v-form>
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="dialog = false">Cancel</v-btn>
          <v-btn
            color="primary"
            variant="elevated"
            :loading="studiosStore.loading"
            @click="saveStudio"
          >
            {{ editingStudio ? 'Save' : 'Create' }}
          </v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>

    <!-- Delete Confirmation Dialog -->
    <v-dialog v-model="deleteDialog" max-width="400">
      <v-card>
        <v-card-title>Delete Studio</v-card-title>
        <v-card-text>
          Are you sure you want to delete "{{ studioToDelete?.name }}"?
          This action cannot be undone.
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="deleteDialog = false">Cancel</v-btn>
          <v-btn
            color="error"
            variant="elevated"
            :loading="studiosStore.loading"
            @click="deleteStudio"
          >
            Delete
          </v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>
  </v-container>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useStudiosStore } from '@/stores/studios'
import { useRecordersStore } from '@/stores/recorders'

const studiosStore = useStudiosStore()
const recordersStore = useRecordersStore()

const dialog = ref(false)
const deleteDialog = ref(false)
const editingStudio = ref(null)
const studioToDelete = ref(null)
const formRef = ref(null)

const form = ref({
  name: '',
  description: '',
  color: '#2196F3',
  enabled: true
})

const colors = [
  '#F44336', '#E91E63', '#9C27B0', '#673AB7',
  '#3F51B5', '#2196F3', '#03A9F4', '#00BCD4',
  '#009688', '#4CAF50', '#8BC34A', '#CDDC39',
  '#FFC107', '#FF9800', '#FF5722', '#795548'
]

function getRecorder(recorderId) {
  if (!recorderId) return null
  return recordersStore.getRecorderById(recorderId)
}

function getRecorderStateColor(recorder) {
  switch (recorder?.state) {
    case 'recording': return 'error'
    case 'paused': return 'warning'
    default: return 'grey'
  }
}

function getRecorderStateIcon(recorder) {
  switch (recorder?.state) {
    case 'recording': return 'mdi-record-circle'
    case 'paused': return 'mdi-pause-circle'
    default: return 'mdi-stop-circle'
  }
}

function availableRecorderOptions(studio) {
  // Include current recorder and unassigned recorders
  return recordersStore.activeRecorders
    .filter(r => !r.studio_id || r.studio_id === studio.id)
    .map(r => ({
      title: r.name,
      value: r.id
    }))
}

async function assignRecorder(studioId, recorderId) {
  if (recorderId) {
    await studiosStore.assignRecorder(studioId, recorderId)
  } else {
    await studiosStore.unassignRecorder(studioId)
  }
  await recordersStore.fetchRecorders()
}

function openCreateDialog() {
  editingStudio.value = null
  form.value = {
    name: '',
    description: '',
    color: '#2196F3',
    enabled: true
  }
  dialog.value = true
}

function openEditDialog(studio) {
  editingStudio.value = studio
  form.value = {
    name: studio.name,
    description: studio.description || '',
    color: studio.color,
    enabled: studio.enabled !== false
  }
  dialog.value = true
}

async function saveStudio() {
  const { valid } = await formRef.value.validate()
  if (!valid) return

  if (editingStudio.value) {
    await studiosStore.updateStudio(editingStudio.value.id, form.value)
  } else {
    await studiosStore.createStudio(form.value)
  }

  dialog.value = false
}

function confirmDelete(studio) {
  studioToDelete.value = studio
  deleteDialog.value = true
}

async function deleteStudio() {
  await studiosStore.deleteStudio(studioToDelete.value.id)
  await recordersStore.fetchRecorders()
  deleteDialog.value = false
}

onMounted(async () => {
  await Promise.all([
    studiosStore.fetchStudios(),
    recordersStore.fetchRecorders()
  ])
})
</script>
