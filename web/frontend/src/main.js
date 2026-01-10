/**
 * Audyn Web - Main Entry Point
 *
 * Copyright: (c) 2026 B. Wynne
 * License: GPLv2 or later
 */

import { createApp } from 'vue'
import { createPinia } from 'pinia'
import App from './App.vue'
import router from './plugins/router'
import vuetify from './plugins/vuetify'

import '@mdi/font/css/materialdesignicons.css'
import './assets/main.css'

const app = createApp(App)

app.use(createPinia())
app.use(router)
app.use(vuetify)

app.mount('#app')
