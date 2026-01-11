<template>
  <v-container fluid class="pa-6">
    <div class="d-flex justify-space-between align-center mb-6">
      <div class="d-flex align-center gap-4">
        <h1 class="text-h4">Archive Files</h1>

        <!-- Delete Selected Button -->
        <v-btn
          v-if="selectedFiles.length > 0"
          color="error"
          variant="tonal"
          prepend-icon="mdi-delete"
          @click="confirmBulkDelete"
        >
          Delete {{ selectedFiles.length }} Selected
        </v-btn>
      </div>

      <div class="d-flex gap-2 align-center">
        <!-- Studio filter -->
        <v-select
          v-model="selectedStudioId"
          :items="studioOptions"
          item-title="title"
          item-value="value"
          label="Studio"
          density="compact"
          hide-details
          clearable
          style="min-width: 150px"
          class="mr-2"
          @update:model-value="onStudioChange"
        />

        <!-- Recorder filter -->
        <v-select
          v-model="selectedRecorderId"
          :items="recorderOptions"
          item-title="title"
          item-value="value"
          label="Recorder"
          density="compact"
          hide-details
          clearable
          style="min-width: 150px"
          class="mr-2"
        />

        <v-text-field
          v-model="search"
          prepend-inner-icon="mdi-magnify"
          label="Search"
          density="compact"
          hide-details
          clearable
          style="max-width: 250px"
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

    <!-- Files Table -->
    <v-card>
      <v-data-table
        v-model="selectedFiles"
        :headers="headers"
        :items="filteredFiles"
        :items-per-page="25"
        item-value="path"
        show-select
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

        <template v-slot:item.location="{ item }">
          <div class="text-caption">
            {{ getFileLocation(item.path) }}
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
    <v-dialog v-model="deleteDialog.show" max-width="500">
      <v-card>
        <v-card-title>
          {{ deleteDialog.bulk ? 'Delete Multiple Files' : 'Delete File' }}
        </v-card-title>
        <v-card-text>
          <template v-if="deleteDialog.bulk">
            <p class="mb-2">Are you sure you want to delete {{ deleteDialog.files.length }} files?</p>
            <v-list density="compact" max-height="200" class="overflow-y-auto">
              <v-list-item v-for="file in deleteDialog.files" :key="file.path" density="compact">
                <template v-slot:prepend>
                  <v-icon size="small" :color="getFileColor(file.format)">
                    {{ getFileIcon(file.format) }}
                  </v-icon>
                </template>
                <v-list-item-title class="text-body-2">{{ file.name }}</v-list-item-title>
              </v-list-item>
            </v-list>
            <p class="mt-2 text-error">This action cannot be undone.</p>
          </template>
          <template v-else>
            Are you sure you want to delete "{{ deleteDialog.file?.name }}"?
            This action cannot be undone.
          </template>
        </v-card-text>
        <v-card-actions>
          <v-spacer />
          <v-btn variant="text" @click="deleteDialog.show = false">Cancel</v-btn>
          <v-btn
            color="error"
            :loading="deleteDialog.loading"
            @click="deleteDialog.bulk ? deleteBulkFiles() : deleteFile()"
          >
            Delete{{ deleteDialog.bulk ? ` ${deleteDialog.files.length} Files` : '' }}
          </v-btn>
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
const allFiles = ref([])  // All files from search
const displayedFiles = ref([])  // Filtered files for display
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
const selectedFiles = ref([])  // Selected file paths for bulk operations
const selectedStudioId = ref(null)
const selectedRecorderId = ref(null)
const deleteDialog = ref({
  show: false,
  file: null,
  files: [],
  bulk: false,
  loading: false
})

// Studio options for filter
const studioOptions = computed(() => [
  { title: 'All Studios', value: null },
  ...studiosStore.studios.map(s => ({
    title: s.name,
    value: s.id
  }))
])

// Recorder options - extracted from file paths
const recorderOptions = computed(() => {
  const recorders = new Set()
  allFiles.value.forEach(f => {
    const parts = f.path.split('/')
    if (parts.length >= 2) {
      // Filter recorders by selected studio if one is selected
      if (!selectedStudioId.value || parts[0] === selectedStudioId.value) {
        recorders.add(parts[1])
      }
    }
  })
  return [
    { title: 'All Recorders', value: null },
    ...Array.from(recorders).sort().map(r => ({
      title: r,
      value: r
    }))
  ]
})

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
  { title: 'Location', key: 'location', sortable: true, width: 200 },
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

// Filter function
function applyFilters() {
  let files = [...allFiles.value]

  // Filter by studio
  if (selectedStudioId.value) {
    const studioPrefix = selectedStudioId.value + '/'
    files = files.filter(f => f.path.startsWith(studioPrefix))
  }

  // Filter by recorder
  if (selectedRecorderId.value) {
    files = files.filter(f => {
      const parts = f.path.split('/')
      return parts.length >= 2 && parts[1] === selectedRecorderId.value
    })
  }

  // Filter by search query
  if (search.value) {
    const query = search.value.toLowerCase()
    files = files.filter(f => f.name.toLowerCase().includes(query))
  }

  displayedFiles.value = files
}

// For backwards compatibility with template
const filteredFiles = computed(() => displayedFiles.value)

// Methods
async function fetchFiles() {
  try {
    // Fetch all files using search endpoint
    const response = await fetch('/api/assets/search?limit=500')
    if (response.ok) {
      const data = await response.json()
      allFiles.value = data.files || []

      // Extract unique directories for navigation
      const dirSet = new Set()
      allFiles.value.forEach(f => {
        const parts = f.path.split('/')
        if (parts.length > 1) {
          dirSet.add(parts[0])
        }
      })

      currentDir.value = {
        path: '',
        name: 'Archive',
        parent: null,
        directories: Array.from(dirSet).sort(),
        files: [],
        total_size: allFiles.value.reduce((sum, f) => sum + f.size, 0),
        file_count: allFiles.value.length
      }

      // Apply filters after fetching
      applyFilters()
    }
  } catch (err) {
    console.error('Failed to fetch files:', err)
  }
}

function onStudioChange(value) {
  selectedStudioId.value = value
  selectedRecorderId.value = null
  applyFilters()
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

function getFileLocation(path) {
  // Extract studio/recorder from path like "studio-a/recorder-1/file.wav"
  const parts = path.split('/')
  if (parts.length >= 2) {
    return `${parts[0]} / ${parts[1]}`
  }
  return parts[0] || ''
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
    file,
    files: [],
    bulk: false,
    loading: false
  }
}

function confirmBulkDelete() {
  // Find the actual file objects from the selected paths
  const filesToDelete = allFiles.value.filter(f => selectedFiles.value.includes(f.path))
  deleteDialog.value = {
    show: true,
    file: null,
    files: filesToDelete,
    bulk: true,
    loading: false
  }
}

async function deleteFile() {
  deleteDialog.value.loading = true
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
    deleteDialog.value.loading = false
  }
}

async function deleteBulkFiles() {
  deleteDialog.value.loading = true
  try {
    const paths = deleteDialog.value.files.map(f => f.path)
    const response = await fetch('/api/assets/delete-bulk', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ paths })
    })

    if (response.ok) {
      const result = await response.json()
      console.log(`Deleted ${result.deleted.length} files`, result)

      // Clear selection
      selectedFiles.value = []

      // Clear preview if deleted
      if (selectedFile.value && paths.includes(selectedFile.value.path)) {
        selectedFile.value = null
      }

      await fetchFiles()
    }
  } catch (err) {
    console.error('Failed to delete files:', err)
  } finally {
    deleteDialog.value.show = false
    deleteDialog.value.loading = false
  }
}

// Watchers
watch(currentPath, () => {
  fetchFiles()
})

// Apply filters when studio changes
watch(selectedStudioId, () => {
  selectedRecorderId.value = null
  applyFilters()
})

// Apply filters when recorder changes
watch(selectedRecorderId, () => {
  applyFilters()
})

// Apply filters when search changes
watch(search, () => {
  applyFilters()
})

// Lifecycle
onMounted(async () => {
  await studiosStore.fetchStudios()
  fetchFiles()
})
</script>
