from typing import Optional

from ymmsl import ParameterValue, Reference, Settings


def has_parameter_type(value: ParameterValue, typ: str) -> bool:
    """Checks whether the value has the given type.

    Args:
        value: A parameter value.
        typ: A parameter type. Valid values are 'str', 'int', 'float',
                'bool', '[float]', and '[[float]]'.

    Returns:
        True if the type of value matches typ.

    Raises:
        ValueError: If the type specified is not valid.
    """
    par_type_to_type = {
            'str': str,
            'int': int,
            'float': float,
            'bool': bool
            }

    if typ in par_type_to_type:
        return isinstance(value, par_type_to_type[typ])
    elif typ == '[float]':
        if isinstance(value, list):
            if len(value) == 0 or isinstance(value[0], float):
                # We don't check everything here, the yMMSL loader does
                # a full type check, so we just need to discriminate.
                return True
        return False
    elif typ == '[[float]]':
        if isinstance(value, list):
            if len(value) == 0 or isinstance(value[0], list):
                # We don't check everything here, the yMMSL loader does
                # a full type check, so we just need to discriminate.
                return True
        return False
    raise ValueError('Invalid parameter type specified: {}'.format(typ))


class SettingsManager:
    """Manages the current settings for a compute element instance.
    """
    def __init__(self) -> None:
        """Create a SettingsManager.

        Initialises the base and overlay layers to an empty
        Settings object.

        A SettingsManager has two layers of settings, a base
        layer that contains an immutable set of parameters set in the
        simulation's yMMSL description, and an overlay layer that holds
        parameter values that have been set at run-time.

        Attributes:
            base: The base layer.
            overlay: The overlay layer.
        """
        self.base = Settings()
        self.overlay = Settings()

    def get_parameter(self, instance: Reference, parameter_name: Reference,
                      typ: Optional[str] = None) -> ParameterValue:
        """Returns the value of a parameter.

        Args:
            instance: The instance that this value is for.
            parameter_name: The name of the parameter to get the value of.
            typ: An optional type designation; if specified the type
                    is checked for a match before returning. Valid
                    values are 'str', 'int', 'float', 'bool',
                    '[float]' and '[[float]]'.

        Raises:
            KeyError: If the parameter has not been set.
            TypeError: If the parameter was set to a value that does
                    not match `typ`.
            ValueError: If an invalid value was specified for `typ`
        """
        for i in range(len(instance), -1, -1):
            if i > 0:
                name = instance[:i] + parameter_name
            else:
                name = parameter_name

            if name in self.overlay:
                value = self.overlay[name]
                break
            elif name in self.base:
                value = self.base[name]
                break
        else:
            raise KeyError(('Parameter value for parameter "{}" was not'
                            ' set.'.format(parameter_name)))

        if typ is not None:
            if not has_parameter_type(value, typ):
                raise TypeError('Value for parameter "{}" is of type {},'
                                ' where {} was expected.'.format(
                                    name, type(value), typ))
        return value
