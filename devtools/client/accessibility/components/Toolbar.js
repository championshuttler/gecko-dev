/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

// React
const {
  createFactory,
  Component,
} = require("devtools/client/shared/vendor/react");
const {
  div,
  span,
} = require("devtools/client/shared/vendor/react-dom-factories");
const PropTypes = require("devtools/client/shared/vendor/react-prop-types");
const { L10N } = require("devtools/client/accessibility/utils/l10n");
const Button = createFactory(
  require("devtools/client/accessibility/components/Button").Button
);
const AccessibilityTreeFilter = createFactory(
  require("devtools/client/accessibility/components/AccessibilityTreeFilter")
);
const AccessibilityPrefs = createFactory(
  require("devtools/client/accessibility/components/AccessibilityPrefs")
);
loader.lazyGetter(this, "SimulationMenuButton", function() {
  return createFactory(
    require("devtools/client/accessibility/components/SimulationMenuButton")
  );
});

const { connect } = require("devtools/client/shared/vendor/react-redux");
const {
  disable,
  updateCanBeDisabled,
} = require("devtools/client/accessibility/actions/ui");

class Toolbar extends Component {
  static get propTypes() {
    return {
      dispatch: PropTypes.func.isRequired,
      accessibility: PropTypes.object.isRequired,
      canBeDisabled: PropTypes.bool.isRequired,
      toolboxDoc: PropTypes.object.isRequired,
      audit: PropTypes.func.isRequired,
      simulate: PropTypes.func,
    };
  }

  constructor(props) {
    super(props);

    this.state = {
      disabling: false,
    };

    this.onDisable = this.onDisable.bind(this);
    this.onCanBeDisabledChange = this.onCanBeDisabledChange.bind(this);
  }

  componentWillMount() {
    this.props.accessibility.on(
      "can-be-disabled-change",
      this.onCanBeDisabledChange
    );
  }

  componentWillUnmount() {
    this.props.accessibility.off(
      "can-be-disabled-change",
      this.onCanBeDisabledChange
    );
  }

  onCanBeDisabledChange(canBeDisabled) {
    this.props.dispatch(updateCanBeDisabled(canBeDisabled));
  }

  onDisable() {
    const { accessibility, dispatch } = this.props;
    this.setState({ disabling: true });

    dispatch(disable(accessibility))
      .then(() => this.setState({ disabling: false }))
      .catch(() => this.setState({ disabling: false }));
  }

  render() {
    const { canBeDisabled, simulate, toolboxDoc, audit } = this.props;
    const { disabling } = this.state;
    const disableButtonStr = disabling
      ? "accessibility.disabling"
      : "accessibility.disable";
    const betaID = "beta";
    let title;
    let isDisabled = false;

    if (canBeDisabled) {
      title = L10N.getStr("accessibility.disable.enabledTitle");
    } else {
      isDisabled = true;
      title = L10N.getStr("accessibility.disable.disabledTitle");
    }

    const optionalSimulationSection = simulate
      ? [
          div({
            role: "separator",
            className: "devtools-separator",
          }),
          SimulationMenuButton({ simulate, toolboxDoc }),
        ]
      : [];

    return div(
      {
        className: "devtools-toolbar",
        role: "toolbar",
      },
      Button(
        {
          className: "disable",
          id: "accessibility-disable-button",
          onClick: this.onDisable,
          disabled: disabling || isDisabled,
          busy: disabling,
          title,
        },
        L10N.getStr(disableButtonStr)
      ),
      div({
        role: "separator",
        className: "devtools-separator",
      }),
      // @remove after release 68 (See Bug 1551574)
      span(
        {
          className: "beta",
          role: "presentation",
          id: betaID,
        },
        L10N.getStr("accessibility.beta")
      ),
      AccessibilityTreeFilter({
        audit,
        describedby: betaID,
        toolboxDoc,
      }),
      // Simulation section is shown if webrender is enabled
      ...optionalSimulationSection,
      AccessibilityPrefs({ toolboxDoc })
    );
  }
}

const mapStateToProps = ({ ui }) => ({
  canBeDisabled: ui.canBeDisabled,
});

// Exports from this module
module.exports = connect(mapStateToProps)(Toolbar);
