import React from 'react';
import { Dial, DialMode } from '.';
import { WidgetUtilities } from 'src/utilities';
import { AxisInfo, JoystickValue, PropertyType } from 'src/shared';


type Props = {
  min?: number;
  max?: number;
  display?: 'VALUE' | 'PERCENT';
  mode?: DialMode;
  size?: number;
  X?: boolean;
  Y?: boolean;
  Z?: boolean;
  value?: number | JoystickValue;
  type?: PropertyType;
  showLabel?: boolean;
  hidePrecision?: boolean;

  onChange?: (res:  number | JoystickValue) => void;
};

export class DialsWrapper extends React.Component<Props> {

  getProperties = (): AxisInfo[] => {
    const { type } = this.props;
    const keys = WidgetUtilities.getPropertyKeys(type);
    const allAxeses = keys.map(key => [ key, 1 ]);

    const axes = [];
    if (this.props.X)
      axes.push(allAxeses[0]);

    if (this.props.Y)
      axes.push(allAxeses[1]);

    if (this.props.Z)
      axes.push(allAxeses[2]);

    if (axes.length === 0)
      axes.push(...allAxeses);

    return axes;
  }

  onChange = (res: number, key?: string) => {
    const { onChange } = this.props;
    let { value } = this.props;
    if (value === undefined) {
      const dials = this.getProperties();
      value = dials.length ? {} : 0;
    }

    if (key) {
      value[key] = res;
    } else {
      value = res;
    }

    onChange?.(value);
  }

  render() {
    const { min, max, mode, value, display, showLabel, hidePrecision } = this.props;
    let { size } = this.props;


    const dials = this.getProperties();
    if (isNaN(size))
      size = 160;

    size = Math.min(size / dials.length, size - 70) - 10;

    let startAngle, endAngle;

    const props = {
      mode,
      min,
      max,
      display,
      size,
      startAngle,
      endAngle,
      hideReset: true,
      hidePrecision,
    };

    return (
      <div className="dial-wrapper-block">

        {!!dials.length &&
          dials.map(proporties => {
            const key = proporties[0];
            return (
              <Dial {...props}
                    key={key}       
                    label={showLabel ? key : undefined}
                    value={value?.[key] ?? 0}
                    onChange={value => this.onChange(value, key)} />
              );
            })
        }

        {!dials.length &&
          <Dial {...props}
                value={typeof value === 'number' ? value : 0}
                onChange={this.onChange} />
          }
      </div>
    );
  }
}
