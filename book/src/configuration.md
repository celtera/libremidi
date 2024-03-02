# Advanced configuration

The `midi_in`, `midi_out` and `midi_observer` objects are configured through a `input_configuration` (resp. `output_`, etc.) object passed in argument to the constructor.

## Custom back-end configuration

Additionnally, each back-end supports back-end specific configuration options, to enable users to tap into advanced features of a given API while retaining the general C++ abstraction.

For instance, this enables to set output buffer sizes, chunking parameters, etc. for back-ends which support the feature.
