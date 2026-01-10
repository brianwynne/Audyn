<template>
  <v-container class="fill-height" fluid>
    <v-row align="center" justify="center">
      <v-col cols="12" sm="8" md="4">
        <v-card class="elevation-12">
          <v-toolbar color="primary" dark flat>
            <v-toolbar-title>
              <v-icon icon="mdi-radio-tower" class="mr-2" />
              AUDYN
            </v-toolbar-title>
          </v-toolbar>

          <v-card-text class="text-center pa-8">
            <v-icon icon="mdi-account-circle" size="80" color="grey" class="mb-4" />

            <h2 class="text-h5 mb-2">Welcome to Audyn</h2>
            <p class="text-grey mb-6">
              Enterprise Audio Capture & Archival
            </p>

            <v-alert
              v-if="authStore.error"
              type="error"
              variant="tonal"
              class="mb-4"
            >
              {{ authStore.error }}
            </v-alert>

            <v-btn
              color="primary"
              size="large"
              block
              :loading="authStore.loading"
              @click="handleLogin"
            >
              <v-icon icon="mdi-microsoft" class="mr-2" />
              Sign in with Microsoft
            </v-btn>

            <v-divider class="my-6" />

            <p class="text-caption text-grey">
              Sign in using your organization's Microsoft account.
              <br />
              Contact your administrator if you don't have access.
            </p>
          </v-card-text>
        </v-card>

        <p class="text-center text-caption text-grey mt-4">
          Audyn v1.0.0 &copy; 2026 B. Wynne
        </p>
      </v-col>
    </v-row>
  </v-container>
</template>

<script setup>
import { onMounted } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAuthStore } from '@/stores/auth'

const router = useRouter()
const route = useRoute()
const authStore = useAuthStore()

async function handleLogin() {
  const success = await authStore.login()

  if (success) {
    const redirect = route.query.redirect || '/'
    router.push(redirect)
  }
}

// Handle OAuth callback
onMounted(async () => {
  const code = route.query.code

  if (code) {
    const success = await authStore.handleCallback(code)
    if (success) {
      const redirect = route.query.state || '/'
      router.push(redirect)
    }
  }
})
</script>
