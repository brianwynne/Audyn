/**
 * Authentication Store
 *
 * Handles Entra ID SSO authentication state.
 */

import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

export const useAuthStore = defineStore('auth', () => {
  // State
  const user = ref(null)
  const token = ref(null)
  const loading = ref(false)
  const error = ref(null)
  const selectedStudioId = ref(null)

  // Getters
  const isAuthenticated = computed(() => !!user.value)
  const userName = computed(() => user.value?.name || 'Unknown')
  const userEmail = computed(() => user.value?.email || '')
  const userRoles = computed(() => user.value?.roles || [])
  const isAdmin = computed(() => userRoles.value.includes('admin') || user.value?.role === 'admin')
  const userStudioId = computed(() => user.value?.studio_id || null)
  const userRole = computed(() => user.value?.role || 'studio')
  const hasSelectedStudio = computed(() => !!selectedStudioId.value)

  // Actions
  async function checkAuth() {
    loading.value = true
    error.value = null

    try {
      const response = await fetch('/auth/me', {
        headers: token.value ? { Authorization: `Bearer ${token.value}` } : {}
      })

      if (response.ok) {
        user.value = await response.json()
      } else if (response.status === 401) {
        // Not authenticated, try dev mode login
        await loginDev()
      }
    } catch (err) {
      console.error('Auth check failed:', err)
      error.value = 'Failed to check authentication'
    } finally {
      loading.value = false
    }
  }

  async function login() {
    loading.value = true
    error.value = null

    try {
      // Redirect to Entra ID login
      const response = await fetch('/auth/login')
      const data = await response.json()

      if (data.dev_mode) {
        // Dev mode - auto-login
        user.value = data.user
        token.value = 'dev-token'
        return true
      }

      // Production - redirect to SSO
      if (data.redirect_url) {
        window.location.href = data.redirect_url
      }

      return false
    } catch (err) {
      console.error('Login failed:', err)
      error.value = 'Login failed'
      return false
    } finally {
      loading.value = false
    }
  }

  async function loginDev() {
    // Development mode auto-login
    try {
      const response = await fetch('/auth/login')
      const data = await response.json()

      if (data.dev_mode && data.user) {
        user.value = data.user
        token.value = 'dev-token'
        return true
      }
    } catch (err) {
      console.error('Dev login failed:', err)
    }
    return false
  }

  async function handleCallback(code) {
    loading.value = true
    error.value = null

    try {
      const response = await fetch(`/auth/callback?code=${code}`)
      const data = await response.json()

      if (data.access_token) {
        token.value = data.access_token
        user.value = data.user
        return true
      }

      throw new Error('No token received')
    } catch (err) {
      console.error('Callback failed:', err)
      error.value = 'Authentication callback failed'
      return false
    } finally {
      loading.value = false
    }
  }

  async function logout() {
    loading.value = true

    try {
      const response = await fetch('/auth/logout', { method: 'POST' })
      const data = await response.json()

      user.value = null
      token.value = null
      selectedStudioId.value = null

      if (data.logout_url) {
        window.location.href = data.logout_url
      }
    } catch (err) {
      console.error('Logout failed:', err)
    } finally {
      loading.value = false
    }
  }

  function setSelectedStudio(studioId) {
    selectedStudioId.value = studioId
  }

  function clearSelectedStudio() {
    selectedStudioId.value = null
  }

  async function fetchSelectedStudio() {
    try {
      const response = await fetch('/api/studios/current-selection')
      if (response.ok) {
        const data = await response.json()
        selectedStudioId.value = data.studio_id
        return data.studio_id
      }
    } catch (err) {
      console.error('Failed to fetch selected studio:', err)
    }
    return null
  }

  return {
    // State
    user,
    token,
    loading,
    error,
    selectedStudioId,

    // Getters
    isAuthenticated,
    userName,
    userEmail,
    userRoles,
    isAdmin,
    userStudioId,
    userRole,
    hasSelectedStudio,

    // Actions
    checkAuth,
    login,
    loginDev,
    handleCallback,
    logout,
    setSelectedStudio,
    clearSelectedStudio,
    fetchSelectedStudio
  }
})
