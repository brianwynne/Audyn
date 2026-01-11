/**
 * Vue Router Configuration
 */

import { createRouter, createWebHistory } from 'vue-router'
import { useAuthStore } from '@/stores/auth'

const routes = [
  {
    path: '/',
    name: 'home',
    redirect: { name: 'studio-select' }  // Everyone starts at studio selection
  },
  {
    path: '/studio-select',
    name: 'studio-select',
    component: () => import('@/views/StudioSelector.vue'),
    meta: { requiresAuth: true, title: 'Select Studio' }
  },
  {
    path: '/studio/:id',
    name: 'studio-view',
    component: () => import('@/views/StudioView.vue'),
    meta: { requiresAuth: true, title: 'Studio' }
  },
  {
    path: '/overview',
    name: 'overview',
    component: () => import('@/views/Overview.vue'),
    meta: { requiresAuth: true, title: 'Overview' }
  },
  {
    path: '/dashboard',
    name: 'dashboard',
    component: () => import('@/views/Dashboard.vue'),
    meta: { requiresAuth: true, title: 'Dashboard' }
  },
  {
    path: '/recorders',
    name: 'recorders',
    component: () => import('@/views/Recorders.vue'),
    meta: { requiresAuth: true, requiresAdmin: true, title: 'Recorders' }
  },
  {
    path: '/recorders/:id',
    name: 'recorder',
    component: () => import('@/views/RecorderDetail.vue'),
    meta: { requiresAuth: true, requiresAdmin: true, title: 'Recorder' }
  },
  {
    path: '/studios',
    name: 'studios',
    component: () => import('@/views/Studios.vue'),
    meta: { requiresAuth: true, requiresAdmin: true, title: 'Studios' }
  },
  {
    path: '/sources',
    name: 'sources',
    component: () => import('@/views/Sources.vue'),
    meta: { requiresAuth: true, requiresAdmin: true, title: 'Sources' }
  },
  {
    path: '/files',
    name: 'files',
    component: () => import('@/views/Files.vue'),
    meta: { requiresAuth: true, requiresAdmin: true, title: 'Files' }
  },
  {
    path: '/settings',
    name: 'settings',
    component: () => import('@/views/Settings.vue'),
    meta: { requiresAuth: true, requiresAdmin: true, title: 'Settings' }
  },
  {
    path: '/login',
    name: 'login',
    component: () => import('@/views/Login.vue'),
    meta: { requiresAuth: false, title: 'Login' }
  }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

// Navigation guard
router.beforeEach(async (to, from, next) => {
  const authStore = useAuthStore()

  // Set page title
  document.title = `${to.meta.title || 'Audyn'} - Audyn`

  if (to.meta.requiresAuth && !authStore.isAuthenticated) {
    // Try to restore session
    await authStore.checkAuth()

    if (!authStore.isAuthenticated) {
      next({ name: 'login', query: { redirect: to.fullPath } })
      return
    }
  }

  // Check admin access
  if (to.meta.requiresAdmin && !authStore.isAdmin) {
    // Redirect non-admins to studio selector
    next({ name: 'studio-select' })
    return
  }

  next()
})

export default router
