module.exports = [
  {
    type: 'heading',
    defaultValue: 'Layer-by-Layer'
  },
  {
    type: 'text',
    defaultValue: 'Customize how your minute gets printed.'
  },
  {
    type: 'section',
    items: [
      {
        type: 'heading',
        defaultValue: 'Time'
      },
      {
        type: 'select',
        messageKey: 'TimeFormat',
        label: 'Time format',
        defaultValue: '0',
        options: [
          { label: 'System', value: '0' },
          { label: '12-hour', value: '1' },
          { label: '24-hour', value: '2' }
        ]
      }
    ]
  },
  {
    type: 'section',
    items: [
      {
        type: 'heading',
        defaultValue: 'Date'
      },
      {
        type: 'toggle',
        messageKey: 'ShowDate',
        label: 'Show date',
        defaultValue: true
      },
      {
        type: 'select',
        messageKey: 'DateFormat',
        label: 'Date format',
        defaultValue: '0',
        options: [
          { label: 'Sun 1 Nov', value: '0' },
          { label: '11/1', value: '1' },
          { label: 'ISO (2026-11-01)', value: '2' }
        ]
      }
    ]
  },
  {
    type: 'section',
    items: [
      {
        type: 'heading',
        defaultValue: 'Display'
      },
      {
        type: 'toggle',
        messageKey: 'ShowProgress',
        label: 'Layer progress',
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'DropAnim',
        label: 'Drop animation',
        defaultValue: true
      },
      {
        type: 'select',
        messageKey: 'Theme',
        label: 'Color theme',
        description: 'Color watches only. B&W stays high-contrast.',
        defaultValue: '0',
        options: [
          { label: 'Classic (cyan / navy)', value: '0' },
          { label: 'Forest', value: '1' },
          { label: 'Ember', value: '2' },
          { label: 'Violet', value: '3' },
          { label: 'Mono', value: '4' }
        ]
      }
    ]
  },
  {
    type: 'submit',
    defaultValue: 'Save'
  }
];
