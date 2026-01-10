/**
 * Vuetify Plugin Configuration
 */

import 'vuetify/styles'
import { createVuetify } from 'vuetify'
import * as components from 'vuetify/components'
import * as directives from 'vuetify/directives'

const audynTheme = {
  dark: true,
  colors: {
    background: '#121212',
    surface: '#1E1E1E',
    'surface-bright': '#2A2A2A',
    'surface-variant': '#424242',
    primary: '#2196F3',
    'primary-darken-1': '#1976D2',
    secondary: '#03DAC6',
    'secondary-darken-1': '#018786',
    error: '#CF6679',
    info: '#2196F3',
    success: '#4CAF50',
    warning: '#FB8C00',
    'on-background': '#FFFFFF',
    'on-surface': '#FFFFFF',
    recording: '#F44336',
    meter: '#4CAF50',
    'meter-peak': '#FF9800',
    'meter-clip': '#F44336'
  }
}

const audynLightTheme = {
  dark: false,
  colors: {
    background: '#FAFAFA',
    surface: '#FFFFFF',
    'surface-bright': '#F5F5F5',
    'surface-variant': '#E0E0E0',
    primary: '#1976D2',
    'primary-darken-1': '#1565C0',
    secondary: '#00897B',
    'secondary-darken-1': '#00796B',
    error: '#D32F2F',
    info: '#1976D2',
    success: '#388E3C',
    warning: '#F57C00',
    recording: '#D32F2F',
    meter: '#388E3C',
    'meter-peak': '#F57C00',
    'meter-clip': '#D32F2F'
  }
}

export default createVuetify({
  components,
  directives,
  theme: {
    defaultTheme: 'audynTheme',
    themes: {
      audynTheme,
      audynLightTheme
    }
  },
  defaults: {
    VBtn: {
      variant: 'flat'
    },
    VCard: {
      elevation: 2
    },
    VTextField: {
      variant: 'outlined',
      density: 'comfortable'
    },
    VSelect: {
      variant: 'outlined',
      density: 'comfortable'
    }
  }
})
