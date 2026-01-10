<template>
  <div class="mini-meter">
    <div
      v-for="channel in levels"
      :key="channel.name"
      class="mini-bar"
    >
      <div class="mini-bar-bg">
        <div
          class="mini-bar-fill"
          :style="{ width: `${getLevelPercent(channel.level_db)}%` }"
          :class="getMeterClass(channel)"
        />
      </div>
    </div>
  </div>
</template>

<script setup>
defineProps({
  levels: {
    type: Array,
    default: () => []
  }
})

function getLevelPercent(db) {
  const level = Math.max(-60, Math.min(0, db))
  return ((level + 60) / 60) * 100
}

function getMeterClass(channel) {
  if (channel.clipping) return 'clip'
  if (channel.level_db > -6) return 'peak'
  if (channel.level_db > -12) return 'high'
  return 'normal'
}
</script>

<style scoped>
.mini-meter {
  display: flex;
  flex-direction: column;
  gap: 2px;
  width: 100%;
}

.mini-bar {
  height: 8px;
}

.mini-bar-bg {
  height: 100%;
  background: rgba(0, 0, 0, 0.3);
  border-radius: 2px;
  overflow: hidden;
}

.mini-bar-fill {
  height: 100%;
  transition: width 0.05s linear;
  border-radius: 2px;
}

.mini-bar-fill.normal {
  background: linear-gradient(to right, #4caf50, #8bc34a);
}

.mini-bar-fill.high {
  background: linear-gradient(to right, #4caf50, #ffc107);
}

.mini-bar-fill.peak {
  background: linear-gradient(to right, #4caf50, #ff9800);
}

.mini-bar-fill.clip {
  background: linear-gradient(to right, #4caf50, #f44336);
}
</style>
