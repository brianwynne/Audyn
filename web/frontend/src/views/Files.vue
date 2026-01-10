<template>
  <v-container fluid class="pa-6">
    <div class="d-flex justify-space-between align-center mb-6">
      <h1 class="text-h4">Archive Files</h1>

      <div class="d-flex gap-2 align-center">
        <!-- Studio filter (admin only) -->
        <v-select
          v-if="authStore.isAdmin"
          v-model="selectedStudioId"
          :items="studioOptions"
          label="Studio"
          density="compact"
          hide-details
          clearable
          style="max-width: 200px"
          class="mr-2"
        />

        <v-text-field
          v-model="search"
          prepend-inner-icon="mdi-magnify"
          label="Search"
          density="compact"
          hide-details
          clearable
          style="max-width: 300px"
        />
        <v-btn icon @click="fetchFiles">
          <v-icon>mdi-refresh</v-icon>
        </v-btn>
      </div>
    </div>

    <!-- Studio indicator for studio users -->
    <v-alert
      v-if="!authStore.isAdmin && currentStudio"
      type="info"
      variant="tonal"
      density="compact"
      class="mb-4"
    >
      <v-icon start>mdi-broadcast</v-icon>
      Viewing recordings for: <strong>{{ currentStudio.name }}</strong>
    </v-alert>

    <!-- Breadcrumb -->
    <v-breadcrumbs :items="breadcrumbs" class="pa-0 mb-4">
      <template v-slot:divider>
        <v-icon icon="mdi-chevron-right" />
      </template>
    </v-breadcrumbs>

    <!-- Directories -->
    <v-row v-if="currentDir.directories.length" class="mb-4">
      <v-col
        v-for="dir in currentDir.directories"
        :key="dir"
        cols="6"
        sm="4"
        md="3"
        lg="2"
      >
        <v-card
          hover
          class="text-center pa-4"
          @click="navigateTo(dir)"
        >
          <v-icon icon="mdi-folder" size="48" color="warning" />
          <div class="mt-2 text-truncate">{{ dir }}</div>
        </v-card>
      </v-col>
    </v-row>

    <!-- Files Table -->
    <v-card>
      <v-data-table
        :headers="headers"
        :items="filteredFiles"
        :items-per-page="25"
        hover
      >
        <template v-slot:item.name="{ item }">
          <div class="d-flex align-center">
            <v-icon
              :icon="getFileIcon(item.format)"
              :color="getFileColor(item.format)"
              class="mr-2"
            />
            {{ item.name }}
          </div>
        </template>

        <template v-slot:item.size="{ item }">
          {{ formatSize(item.size) }}
        </template>

        <template v-slot:item.modified="{ item }">
          {{ formatDate(item.modified) }}
        </template>

        <template v-slot:item.actions="{ item }">
          <v-btn
            icon
            size="small"
            variant="text"
            @click="previewFile(item)"
          >
            <v-icon>mdi-play</v-icon>
          </v-btn>
          <v-btn
            icon
            size="small"
            variant="text"
            :href="`/api/assets/download/${item.path}`"
            target="_blank"
          >
            <v-icon>mdi-download</v-icon>
          </v-btn>
          <v-btn
            v-if="canDeleteFile(item)"
            icon
            size="small"
            variant="text"
            color="error"
            @click="confirmDelete(item)"
          >
            <v-icon>mdi-delete</v-icon>
          </v-btn>
        </template>
      </v-data-table>
    </v-card>

    <!-- Audio Player -->
    <v-card v-if="selectedFile" class="mt-4">
      <v-card-title>
        <v-icon icon="mdi-play-circle" class="mr-2" />
        Preview: {{ selectedFile.name }}
        <v-spacer />
        <v-btn icon size="small" @click="selectedFile = null">
          <v-icon>mdi-close</v-icon>
        </v-btn>
      </v-card-title>
      <v-card-text>
        <audio
          ref="audioPlayer"
          :src="`/api/stream/preview/${selectedFile.path}`"
          controls
          style="width: 100%"
        />
      </v-card-text>
    </v-card>

    <!-- Delete Confirmation -->
    <v-dialog v-model="deleteDialog.show" max-width="400">
      <v-card>
        <v-card-title>Delete File</v-card-title>
        <v-card-text>
          Are you sure you want to delete "{{ deleteDialog.file?.name }}"?
          This action cannot be undone.
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="deleteDialog.show = false">Cancel</v-btn>
          <v-btn color="error" @click="deleteFile">Delete</v-btn>
        </v-card-actions>
      </v-card>
    </v-dialog>
  </v-container>
</template>

<script setup>
import { ref, computed, onMounted, watch } from 'vue'
import { useAuthStore } from '@/stores/auth'
import { useStudiosStore } from '@/stores/studios'

const authStore = useAuthStore()
const studiosStore = useStudiosStore()

// State
const currentPath = ref('')
const currentDir = ref({
  path: '',
  name: 'Archive',
  parent: null,
  directories: [],
  files: [],
  total_size: 0,
  file_count: 0
})
const search = ref('')
const selectedFile = ref(null)
const selectedStudioId = ref(null)
const deleteDialog = ref({
  show: false,
  file: null
})

// Studio options for admin filter
const studioOptions = computed(() => [
  { title: 'All Studios', value: null },
  ...studiosStore.studios.map(s => ({
    title: s.name,
    value: s.id
  }))
])

// Current studio for studio users
const currentStudio = computed(() => {
  if (authStore.isAdmin) {
    return selectedStudioId.value
      ? studiosStore.getStudioById(selectedStudioId.value)
      : null
  }
  return authStore.userStudioId
    ? studiosStore.getStudioById(authStore.userStudioId)
    : null
})

// Check if user can delete a file
function canDeleteFile(file) {
  // Admins can delete anything
  if (authStore.isAdmin) return true

  // Studio users can only delete files from their own studio
  // (In a real implementation, files would have studio_id metadata)
  // For now, allow deletion if viewing their studio's files
  return !!currentStudio.value
}

// Table headers
const headers = [
  { title: 'Name', key: 'name', sortable: true },
  { title: 'Size', key: 'size', sortable: true, width: 120 },
  { title: 'Modified', key: 'modified', sortable: true, width: 180 },
  { title: 'Format', key: 'format', sortable: true, width: 100 },
  { title: 'Actions', key: 'actions', sortable: false, width: 150 }
]

// Computed
const breadcrumbs = computed(() => {
  const items = [
    { title: 'Archive', disabled: false, onClick: () => navigateTo('') }
  ]

  if (currentPath.value) {
    const parts = currentPath.value.split('/')
    let path = ''
    for (const part of parts) {
      path = path ? `${path}/${part}` : part
      const p = path
      items.push({
        title: part,
        disabled: false,
        onClick: () => navigateTo(p)
      })
    }
  }

  return items
})

const filteredFiles = computed(() => {
  if (!search.value) return currentDir.value.files

  const query = search.value.toLowerCase()
  return currentDir.value.files.filter(f =>
    f.name.toLowerCase().includes(query)
  )
})

// Methods
async function fetchFiles() {
  try {
    let url = `/api/assets/browse?path=${encodeURIComponent(currentPath.value)}`

    // Add studio filter
    const studioId = authStore.isAdmin ? selectedStudioId.value : authStore.userStudioId
    if (studioId) {
      url += `&studio_id=${encodeURIComponent(studioId)}`
    }

    const response = await fetch(url)
    if (response.ok) {
      currentDir.value = await response.json()
    }
  } catch (err) {
    console.error('Failed to fetch files:', err)
  }
}

function navigateTo(path) {
  if (typeof path === 'string') {
    currentPath.value = path
  } else {
    // It's a directory name, append to current path
    currentPath.value = currentPath.value
      ? `${currentPath.value}/${path}`
      : path
  }
}

function getFileIcon(format) {
  switch (format) {
    case 'wav': return 'mdi-file-music'
    case 'opus':
    case 'ogg': return 'mdi-file-music-outline'
    case 'mp3': return 'mdi-file-music'
    default: return 'mdi-file'
  }
}

function getFileColor(format) {
  switch (format) {
    case 'wav': return 'blue'
    case 'opus':
    case 'ogg': return 'purple'
    case 'mp3': return 'green'
    default: return 'grey'
  }
}

function formatSize(bytes) {
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`
}

function formatDate(dateStr) {
  return new Date(dateStr).toLocaleString()
}

function previewFile(file) {
  selectedFile.value = file
}

function confirmDelete(file) {
  deleteDialog.value = {
    show: true,
    file
  }
}

async function deleteFile() {
  try {
    const response = await fetch(`/api/assets/file/${deleteDialog.value.file.path}`, {
      method: 'DELETE'
    })

    if (response.ok) {
      await fetchFiles()
      if (selectedFile.value?.path === deleteDialog.value.file.path) {
        selectedFile.value = null
      }
    }
  } catch (err) {
    console.error('Failed to delete file:', err)
  } finally {
    deleteDialog.value.show = false
  }
}

// Watchers
watch(currentPath, () => {
  fetchFiles()
})

watch(selectedStudioId, () => {
  fetchFiles()
})

// Lifecycle
onMounted(async () => {
  await studiosStore.fetchStudios()
  fetchFiles()
})
</script>
